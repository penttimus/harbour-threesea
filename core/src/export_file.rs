// SPDX-License-Identifier: AGPL-3.0-only
//! Publication and recovery for archives written into user-selected directories.
use std::{
    fs::{self, File, OpenOptions},
    io,
    os::unix::fs::{MetadataExt as _, OpenOptionsExt as _},
    path::{Path, PathBuf},
};

use crate::{
    app::ApiError,
    store::{Store, StoreError},
};
use rusqlite::params;

pub const MIGRATION: &str = "CREATE TABLE export_files (
    path TEXT PRIMARY KEY, directory TEXT NOT NULL, device INTEGER NOT NULL,
    inode INTEGER NOT NULL, pending INTEGER NOT NULL, size INTEGER, modified INTEGER
);";

pub struct ExportFile<'a> {
    store: &'a Store,
    path: PathBuf,
    device: u64,
    inode: u64,
}

fn write_error(e: io::Error) -> ApiError {
    match e.kind() {
        io::ErrorKind::StorageFull | io::ErrorKind::QuotaExceeded => ApiError::BackupNoRoom,
        _ => ApiError::BackupWrite(e.to_string()),
    }
}

impl<'a> ExportFile<'a> {
    pub fn create(store: &'a Store, directory: &Path) -> Result<(Self, File), ApiError> {
        let dir = fs::canonicalize(directory).map_err(write_error)?;
        let path = dir.join(format!(
            ".threesea-{:032x}.zip.part",
            rand::random::<u128>()
        ));
        let file = OpenOptions::new()
            .write(true)
            .create_new(true)
            .mode(0o600)
            .open(&path)
            .map_err(write_error)?;
        let meta = file.metadata().map_err(write_error)?;
        let guard = Self {
            store,
            path,
            device: meta.dev(),
            inode: meta.ino(),
        };
        guard.store.conn.execute(
            "INSERT INTO export_files(path,directory,device,inode,pending) VALUES (?1,?2,?3,?4,1)",
            params![
                guard.path.to_string_lossy(),
                dir.to_string_lossy(),
                guard.device.cast_signed(),
                guard.inode.cast_signed(),
            ],
        ).map_err(StoreError::from)?;
        Ok((guard, file))
    }

    pub fn publish(&self, name: &str, track: bool) -> Result<PathBuf, ApiError> {
        use rustix::fs::{CWD, RenameFlags, renameat_with};
        let meta = fs::symlink_metadata(&self.path).map_err(write_error)?;
        if !meta.is_file() || (meta.dev(), meta.ino()) != (self.device, self.inode) {
            return Err(ApiError::BackupWrite("temporary file changed".into()));
        }
        let mut dest = self.path.with_file_name(name);
        loop {
            let published = match renameat_with(CWD, &self.path, CWD, &dest, RenameFlags::NOREPLACE)
            {
                Err(
                    rustix::io::Errno::NOSYS
                    | rustix::io::Errno::INVAL
                    | rustix::io::Errno::OPNOTSUPP,
                ) => {
                    // link() has the same exclusive destination semantics on older kernels.
                    fs::hard_link(&self.path, &dest).and_then(|()| fs::remove_file(&self.path))
                }
                result => result.map_err(io::Error::from),
            };
            match published {
                Ok(()) => break,
                Err(e) if e.kind() == io::ErrorKind::AlreadyExists => {
                    let stem = name.strip_suffix(".zip").unwrap_or(name);
                    dest.set_file_name(format!("{stem}-{:016x}.zip", rand::random::<u64>()));
                }
                Err(e) => return Err(write_error(e)),
            }
        }
        if track {
            self.store
                .conn
                .execute(
                    "UPDATE OR REPLACE export_files SET path=?1,pending=0,size=?2,modified=?3 WHERE path=?4",
                    params![
                        dest.to_string_lossy(),
                        meta.size().cast_signed(),
                        meta.mtime(),
                        self.path.to_string_lossy()
                    ],
                )
                .map_err(StoreError::from)?;
        }
        Ok(dest)
    }
}

impl Drop for ExportFile<'_> {
    fn drop(&mut self) {
        if remove_owned(&self.path, self.device, self.inode, None).is_ok() {
            let _ = self.store.conn.execute(
                "DELETE FROM export_files WHERE path=?1 AND pending=1",
                [self.path.to_string_lossy()],
            );
        }
    }
}

fn remove_owned(
    path: &Path,
    device: u64,
    inode: u64,
    completed: Option<(u64, i64)>,
) -> io::Result<()> {
    if owned(path, device, inode, completed)? {
        fs::remove_file(path)?;
    }
    Ok(())
}

fn owned(path: &Path, device: u64, inode: u64, completed: Option<(u64, i64)>) -> io::Result<bool> {
    match fs::symlink_metadata(path) {
        Ok(m) => Ok(m.is_file()
            && (m.dev(), m.ino()) == (device, inode)
            && completed.is_none_or(|expected| expected == (m.size(), m.mtime()))),
        Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(false),
        Err(e) => Err(e),
    }
}

pub fn recover(store: &Store) -> Result<(), StoreError> {
    let mut stmt = store
        .conn
        .prepare("SELECT path, device, inode FROM export_files WHERE pending=1")?;
    let rows = stmt
        .query_map([], |r| {
            Ok((
                r.get::<_, String>(0)?,
                r.get::<_, i64>(1)?,
                r.get::<_, i64>(2)?,
            ))
        })?
        .collect::<Result<Vec<_>, _>>()?;
    for (path, dev, ino) in rows {
        let file = Path::new(&path);
        // An absent SD card can expose an empty mount point. Keep its journal for the next start.
        if file
            .parent()
            .and_then(|dir| fs::metadata(dir).ok())
            .is_none_or(|dir| dir.dev() != dev.cast_unsigned())
        {
            continue;
        }
        if remove_owned(file, dev.cast_unsigned(), ino.cast_unsigned(), None).is_ok() {
            store
                .conn
                .execute("DELETE FROM export_files WHERE path=?1", [path])?;
        }
    }
    Ok(())
}

pub fn rotate(store: &Store, directory: &Path, keep: usize) -> Result<usize, StoreError> {
    if keep == 0 {
        return Ok(0);
    }
    let mut stmt = store.conn.prepare(
        "SELECT path,device,inode,size,modified FROM export_files
         WHERE directory=?1 AND pending=0 ORDER BY rowid DESC",
    )?;
    let rows = stmt
        .query_map([directory.to_string_lossy()], |r| {
            Ok((
                r.get::<_, String>(0)?,
                r.get::<_, i64>(1)?,
                r.get::<_, i64>(2)?,
                r.get::<_, i64>(3)?,
                r.get::<_, i64>(4)?,
            ))
        })?
        .collect::<Result<Vec<_>, _>>()?;
    let mut removed = 0;
    let mut retained = 0;
    for (path, dev, ino, size, modified) in rows {
        let file = Path::new(&path);
        let stamp = Some((size.cast_unsigned(), modified));
        match owned(file, dev.cast_unsigned(), ino.cast_unsigned(), stamp) {
            Ok(true) if retained < keep => {
                retained += 1;
                continue;
            }
            Ok(true) => {
                if fs::remove_file(file).is_err() {
                    continue;
                }
                removed += 1;
            }
            Ok(false) => {}
            Err(_) => continue,
        }
        store
            .conn
            .execute("DELETE FROM export_files WHERE path=?1", [path])?;
    }
    Ok(removed)
}

/// Already compressed formats need only ZIP storage. Check signatures because downloaded
/// attachments can have arbitrary names or MIME labels.
pub fn compression(data: &[u8]) -> zip::CompressionMethod {
    if data.starts_with(b"\xff\xd8\xff")
        || data.starts_with(b"\x89PNG\r\n\x1a\n")
        || data.starts_with(b"OggS")
        || data.starts_with(b"GIF87a")
        || data.starts_with(b"GIF89a")
        || data.starts_with(b"PK\x03\x04")
        || data.get(4..8) == Some(b"ftyp")
        || (data.starts_with(b"RIFF") && data.get(8..12) == Some(b"WEBP"))
    {
        zip::CompressionMethod::Stored
    } else {
        zip::CompressionMethod::Deflated
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::os::unix::fs::PermissionsExt as _;

    #[test]
    fn recovery_collision_and_retention_respect_ownership() {
        let dir = tempfile::tempdir().unwrap();
        let folder = dir.path();
        let store = Store::open(&folder.join("one.db"), &[1; 32]).unwrap();
        let other = Store::open(&folder.join("two.db"), &[2; 32]).unwrap();
        let publish = |owner: &Store, name| {
            let (partial, file) = ExportFile::create(owner, folder).unwrap();
            assert_eq!(file.metadata().unwrap().permissions().mode() & 0o777, 0o600);
            partial.publish(name, true).unwrap()
        };
        let foreign = publish(&other, "backup.zip");
        let first = publish(&store, "backup.zip");
        assert_ne!(foreign, first);
        let missing = publish(&store, "new.zip");
        fs::remove_file(&missing).unwrap();
        let replaced = publish(&store, "replacement.zip");
        fs::rename(&replaced, folder.join("moved")).unwrap();
        fs::write(&replaced, b"someone else's file").unwrap();
        assert_eq!(rotate(&store, folder, 1).unwrap(), 0);
        assert!(first.exists() && foreign.exists());
        assert_eq!(fs::read(&replaced).unwrap(), b"someone else's file");
        let newer = publish(&store, "new.zip");
        assert_eq!(rotate(&store, folder, 1).unwrap(), 1);
        assert!(newer.exists() && foreign.exists() && !first.exists());

        let (interrupted, _) = ExportFile::create(&store, folder).unwrap();
        let partial = interrupted.path.clone();
        std::mem::forget(interrupted);
        recover(&other).unwrap();
        assert!(partial.exists());
        recover(&store).unwrap();
        assert!(!partial.exists());
    }
}
