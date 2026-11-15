// SPDX-License-Identifier: AGPL-3.0-only
//! Reporting spam: a chat that may be spam offers a report to Threema's directory, and to block and
//! delete along with it (`docs/protocol-notes.md`). The rules follow iOS 7.5.2's written steps
//! (`SpamDetector`, `GroupSpamDetector`) with Android's further exclusions
//! (`ComposeMessageFragment.isPossibleSpamContact`): named, blocked and predefined contacts. A
//! group's report goes as on Android 6.6.0 (`ReportGroupFlow`): it waits in the store and is sent
//! in the background, and what the user chose happens at once.

use libthreema::{
    common::keys::ClientKey,
    model::contact::Contact,
    protobuf::d2d_sync::contact::{AcquaintanceLevel, VerificationLevel},
};
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};

use crate::{
    app::{ApiError, App, Shared},
    app_messages::Conversation,
    app_multidevice::SyncChange,
    detached::Lane,
    directory::{self, DirectoryError},
    store::keys,
};

/// A chat's decision: `dismissed` or `reported`, after which it never offers a report again.
const SETTING_PREFIX: &str = "spamReporting:";
/// The first message came this long after the contact was created at most: an old contact that
/// writes for the first time is somebody the user knows.
const FIRST_MESSAGE_WITHIN_MS: i64 = 7 * 24 * 60 * 60 * 1000;
/// Tries before a group's report is given up (Android: eight, over about an hour).
const GROUP_REPORT_TRIES: u32 = 8;

/// A group's report the directory hasn't taken yet (Android 6.6.0's
/// `ReportGroupToDirectoryServerTask`): what it sends is kept, as the group may be gone by then.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
struct PendingGroupReport {
    creator: String,
    nickname: Option<String>,
    group_id: u64,
    members: usize,
    tries: u32,
}

impl PendingGroupReport {
    fn is_for(&self, other: &Self) -> bool {
        self.creator == other.creator && self.group_id == other.group_id
    }
}

/// The directory answered, if only to say no: another try gets the same answer.
fn answered(error: &DirectoryError) -> bool {
    match error {
        DirectoryError::Server(_) | DirectoryError::ReportRefused | DirectoryError::Unexplained => {
            true
        }
        DirectoryError::Status(status) => (400..500).contains(status),
        DirectoryError::Network(_)
        | DirectoryError::RateLimited
        | DirectoryError::InvalidResponse(_) => false,
    }
}

impl App {
    /// A contact the user has no relation to: unverified, unnamed, not blocked, not predefined.
    /// IDs of gateways (`*…`) can't be reported: the directory takes alphanumeric ones only.
    fn unknown_contact(&self, contact: &Contact) -> Result<bool, ApiError> {
        let id = contact.identity.to_string();
        let named = |name: &Option<String>| name.as_deref().is_some_and(|n| !n.is_empty());
        Ok(id.chars().all(char::is_alphanumeric)
            && contact.verification_level == VerificationLevel::Unverified
            && !named(&contact.first_name)
            && !named(&contact.last_name)
            && !self
                .config
                .predefined_contacts
                .contains_key(&contact.identity)
            && !self.app_settings()?.blocked_contacts.contains(&id))
    }

    /// Only messages from them so far, the first soon after the contact came to be.
    fn messages_suggest_spam(&self, key: &str, contact: &Contact) -> Result<bool, ApiError> {
        if self.store.has_outgoing_content(key)? {
            return Ok(false);
        }
        let created = i64::try_from(contact.created_at).unwrap_or(i64::MAX);
        Ok(self
            .store
            .earliest_incoming_content(key)?
            .is_some_and(|first| first.saturating_sub(created) <= FIRST_MESSAGE_WITHIN_MS))
    }

    /// Who to report for this chat, if it may be spam: the contact, or the group's creator.
    fn spam_suspect(
        &self,
        key: &str,
        conversation: &Conversation,
    ) -> Result<Option<Contact>, ApiError> {
        if self
            .store
            .setting(&format!("{SETTING_PREFIX}{key}"))?
            .is_some()
        {
            return Ok(None);
        }
        let suspect = match conversation {
            Conversation::Contact(contact) => (contact.acquaintance_level
                != AcquaintanceLevel::GroupOrDeleted)
                .then(|| contact.clone()),
            Conversation::Group(group) => {
                let creator = group.identity.creator_identity.to_string();
                if group.left || creator == self.user()? {
                    None
                } else {
                    self.store.contact(&creator)?
                }
            }
            Conversation::List(_) => None,
        };
        let Some(suspect) = suspect else {
            return Ok(None);
        };
        Ok(
            (self.unknown_contact(&suspect)? && self.messages_suggest_spam(key, &suspect)?)
                .then_some(suspect),
        )
    }

    /// `spamPotential`: whether this chat offers a report.
    pub(crate) fn spam_potential(&self, identity: &str) -> Result<Value, ApiError> {
        let (key, conversation) = self.readable_conversation(identity)?;
        Ok(json!({ "potential": self.spam_suspect(&key, &conversation)?.is_some() }))
    }

    /// `dismissSpam`: not spam, don't offer it again.
    pub(crate) fn dismiss_spam(&self, identity: &str) -> Result<Value, ApiError> {
        let (key, _) = self.readable_conversation(identity)?;
        self.store
            .set_setting(&format!("{SETTING_PREFIX}{key}"), "dismissed")?;
        Ok(json!({}))
    }

    /// `reportSpam`: report to Threema's directory. A contact's: blocking and deleting follow only
    /// once the report is through, so that a failed one can be tried again from the same chat (as
    /// on Android). A group's waits in the store and goes in the background, and blocking, leaving
    /// and deleting happen now (Android 6.6.0).
    pub(crate) async fn report_spam(
        &mut self,
        identity: &str,
        block: bool,
    ) -> Result<Value, ApiError> {
        let (key, conversation) = self.readable_conversation(identity)?;
        let suspect = self
            .spam_suspect(&key, &conversation)?
            .ok_or_else(|| ApiError::Internal("nothing to report in this chat".into()))?;
        let suspect_id = suspect.identity.to_string();
        let nickname = suspect.nickname;
        if let Conversation::Group(group) = &conversation {
            self.queue_group_report(PendingGroupReport {
                creator: suspect_id.clone(),
                nickname,
                group_id: group.identity.group_id,
                // `members` is everyone but us.
                members: group.members.len() + 1,
                tries: 0,
            })?;
            let done = self.after_report(&key, &conversation, &suspect_id, block)?;
            self.detached_quietly(
                Lane::Free,
                |shared| async move { shared.send_group_reports().await },
                |_, ()| {},
            );
            return Ok(done);
        }
        let own = self.store.identity()?.ok_or(ApiError::NoIdentity)?;
        let client_key = ClientKey::from(*own.client_key);
        let https = self.https.clone();
        let suspect = suspect_id.clone();
        self.detached_then(
            Lane::Free,
            move |_| async move {
                directory::report_identity(
                    &https,
                    &client_key,
                    &own.threema_id,
                    &suspect,
                    nickname.as_deref(),
                )
                .await
            },
            move |app, reported| {
                reported?;
                app.after_report(&key, &conversation, &suspect_id, block)
            },
        )
        .await
    }

    /// A group's report to send, once (the group can be reported only once anyway).
    fn queue_group_report(&self, report: PendingGroupReport) -> Result<(), ApiError> {
        let mut pending = self.pending_group_reports();
        if !pending.iter().any(|queued| queued.is_for(&report)) {
            pending.push(report);
        }
        self.set_pending_group_reports(&pending)?;
        Ok(())
    }

    /// The report is through: remember it, and block and delete if that was asked for.
    fn after_report(
        &mut self,
        key: &str,
        conversation: &Conversation,
        suspect: &str,
        block: bool,
    ) -> Result<Value, ApiError> {
        self.store
            .set_setting(&format!("{SETTING_PREFIX}{key}"), "reported")?;
        tracing::info!(block, "spam reported");
        if block {
            // As Android 6.6.0 does for a group (`hangUpAnyAssociatedCall`): no call with them
            // goes on, nor the group's.
            if self
                .calls
                .current
                .as_ref()
                .is_some_and(|call| call.peer == suspect)
            {
                self.call_end_with_peer();
            }
            self.end_group_call(key);
            self.set_blocked(suspect, true)?;
            self.set_sync_excluded(suspect, true)?;
            self.queue_reflection(SyncChange::Settings, &json!({}));
            if matches!(conversation, Conversation::Group(_)) {
                let left = self.leave_group(key)?;
                let change = SyncChange::Group {
                    previous_members: None,
                    key: Some(key.to_owned()),
                    created: false,
                };
                self.queue_reflection(change, &left);
            }
            self.delete_conversation(key)?;
        }
        Ok(json!({}))
    }
}

impl Shared {
    fn pending_group_reports(&self) -> Vec<PendingGroupReport> {
        self.store
            .setting(keys::SPAM_GROUP_REPORTS)
            .ok()
            .flatten()
            .and_then(|json| serde_json::from_str(&json).ok())
            .unwrap_or_default()
    }

    fn set_pending_group_reports(
        &self,
        pending: &[PendingGroupReport],
    ) -> Result<(), crate::store::StoreError> {
        if pending.is_empty() {
            self.store.delete_setting(keys::SPAM_GROUP_REPORTS)
        } else {
            let json = serde_json::to_string(pending).expect("serializable");
            self.store.set_setting(keys::SPAM_GROUP_REPORTS, &json)
        }
    }

    /// Sends the groups' reports still waiting: right after the user's, after connecting, and with
    /// each round of maintenance. One round at a time. A report goes once the directory answered
    /// it, a refusal too, or after [`GROUP_REPORT_TRIES`].
    pub(crate) async fn send_group_reports(&self) {
        if self.group_reports_sending.replace(true) {
            return;
        }
        let pending = self.pending_group_reports();
        let own = self.store.identity().ok().flatten();
        if let Some(own) = own.filter(|_| !pending.is_empty() && !self.store.directory_resting()) {
            let client_key = ClientKey::from(*own.client_key);
            let mut kept = Vec::new();
            for mut report in pending.iter().cloned() {
                let sent = directory::report_group(
                    &self.https,
                    &client_key,
                    &own.threema_id,
                    &report.creator,
                    report.nickname.as_deref(),
                    report.group_id,
                    report.members,
                )
                .await;
                match sent {
                    Ok(()) => tracing::info!("group reported as spam"),
                    Err(error) if answered(&error) => {
                        tracing::warn!(kind = ApiError::from(error).kind(), "group report refused");
                    }
                    Err(error) => {
                        report.tries += 1;
                        tracing::warn!(
                            kind = ApiError::from(error).kind(),
                            tries = report.tries,
                            "group report not sent"
                        );
                        if report.tries < GROUP_REPORT_TRIES {
                            kept.push(report);
                        }
                    }
                }
            }
            // What was queued meanwhile stays as well.
            let mut now = self.pending_group_reports();
            now.retain(|queued| !pending.iter().any(|sent| sent.is_for(queued)));
            now.extend(kept);
            if let Err(error) = self.set_pending_group_reports(&now) {
                tracing::warn!(
                    kind = ApiError::from(error).kind(),
                    "group reports not kept"
                );
            }
        }
        self.group_reports_sending.set(false);
    }
}

#[cfg(test)]
mod tests {
    use libthreema::common::GroupIdentity;
    use zeroize::Zeroizing;

    use super::*;
    use crate::{
        app::StartConfig,
        contacts::test_contact,
        groups::Group,
        store::{StoredIdentity, keys},
    };

    const USER: &str = "ABCD1234";
    const SUSPECT: &str = "EFGH5678";
    const CREATED: i64 = 1_000_000;
    const DAY: i64 = 24 * 60 * 60 * 1000;

    fn app() -> (tempfile::TempDir, App) {
        let dir = tempfile::tempdir().unwrap();
        let app = App::new(
            &StartConfig {
                data_dir: dir.path().to_owned(),
                app_version: "0.1.0".into(),
                language: "en".into(),
                country: "US".into(),
                model: "test".into(),
                os_version: "5.1".into(),
                pictures_dir: None,
                calls: false,
                log_level: None,
                update_check: false,
            },
            &[3; 32],
        )
        .unwrap();
        app.store
            .set_setting(keys::LICENSE_KEY, "ABCDE-12345")
            .unwrap();
        app.store
            .set_identity(&StoredIdentity {
                threema_id: USER.into(),
                client_key: Zeroizing::new([7; 32]),
                server_group: "30".into(),
            })
            .unwrap();
        (dir, app)
    }

    /// The contact as a stranger who wrote one message right after appearing, `change` applied.
    fn suspect(app: &App, change: impl FnOnce(&mut libthreema::model::contact::ContactInit)) {
        let mut init = test_contact(SUSPECT);
        init.created_at = CREATED as u64;
        init.verification_level = VerificationLevel::Unverified;
        init.acquaintance_level = AcquaintanceLevel::Direct;
        change(&mut init);
        app.store.upsert_contact(&init).unwrap();
    }

    fn potential(app: &App, identity: &str) -> bool {
        app.spam_potential(identity).unwrap()["potential"] == true
    }

    fn message(app: &App, conversation: &str, at: i64) {
        app.store
            .insert_incoming(conversation, at as u64, "text", "buy now", at)
            .unwrap();
    }

    #[test]
    fn a_strangers_first_messages_may_be_spam() {
        let (_dir, app) = app();
        suspect(&app, |_| {});
        assert!(!potential(&app, SUSPECT), "nothing written yet");
        message(&app, SUSPECT, CREATED + DAY);
        assert!(potential(&app, SUSPECT));
    }

    #[test]
    fn a_status_line_is_not_a_message() {
        let (_dir, app) = app();
        suspect(&app, |_| {});
        app.store.insert_status(SUSPECT, "group|created").unwrap();
        assert!(!potential(&app, SUSPECT));
    }

    #[test]
    fn who_is_not_a_stranger() {
        let cases: [fn(&mut libthreema::model::contact::ContactInit); 3] = [
            |c| c.verification_level = VerificationLevel::ServerVerified,
            |c| c.first_name = Some("Ann".into()),
            |c| c.acquaintance_level = AcquaintanceLevel::GroupOrDeleted,
        ];
        for change in cases {
            let (_dir, app) = app();
            suspect(&app, change);
            message(&app, SUSPECT, CREATED + DAY);
            assert!(!potential(&app, SUSPECT));
        }
        // Blocked.
        let (_dir, mut app) = app();
        suspect(&app, |_| {});
        message(&app, SUSPECT, CREATED + DAY);
        app.set_blocked(SUSPECT, true).unwrap();
        assert!(!potential(&app, SUSPECT));
    }

    #[test]
    fn a_gateway_cannot_be_reported() {
        let (_dir, app) = app();
        let mut init = test_contact("*GATEWAY");
        init.created_at = CREATED as u64;
        init.verification_level = VerificationLevel::Unverified;
        init.acquaintance_level = AcquaintanceLevel::Direct;
        app.store.upsert_contact(&init).unwrap();
        message(&app, "*GATEWAY", CREATED + DAY);
        assert!(!potential(&app, "*GATEWAY"));
    }

    #[test]
    fn a_reply_or_an_old_contact_rules_it_out() {
        let (_dir, app) = app();
        suspect(&app, |_| {});
        message(&app, SUSPECT, CREATED + DAY);
        app.store
            .insert_outgoing(SUSPECT, 1, "text", "who are you?")
            .unwrap();
        assert!(!potential(&app, SUSPECT));

        let (_dir, app) = self::app();
        suspect(&app, |_| {});
        message(&app, SUSPECT, CREATED + 7 * DAY + 1);
        assert!(!potential(&app, SUSPECT));
    }

    #[test]
    fn a_dismissed_chat_stays_quiet() {
        let (_dir, app) = app();
        suspect(&app, |_| {});
        message(&app, SUSPECT, CREATED + DAY);
        app.dismiss_spam(SUSPECT).unwrap();
        assert!(!potential(&app, SUSPECT));
    }

    fn group(creator: &str, left: bool) -> Group {
        let group_id = 0x1234_u64;
        Group {
            key: format!("g:{creator}:{group_id:016x}"),
            identity: GroupIdentity {
                creator_identity: creator.parse().unwrap(),
                group_id,
            },
            name: "Offers".into(),
            members: vec![SUSPECT.into()],
            left,
            created_at: CREATED,
            picture: None,
            picture_version: 0,
        }
    }

    #[test]
    fn a_strangers_group_may_be_spam() {
        let (_dir, app) = app();
        suspect(&app, |c| {
            // A group's creator is usually only known from the group.
            c.acquaintance_level = AcquaintanceLevel::GroupOrDeleted;
        });
        let theirs = group(SUSPECT, false);
        app.store.upsert_group(&theirs).unwrap();
        message(&app, &theirs.key, CREATED + DAY);
        assert!(potential(&app, &theirs.key));

        // Not one we left.
        let left = Group {
            left: true,
            ..theirs
        };
        app.store.upsert_group(&left).unwrap();
        assert!(!potential(&app, &left.key));

        // Not our own.
        let ours = group(USER, false);
        app.store.upsert_group(&ours).unwrap();
        message(&app, &ours.key, CREATED + DAY);
        assert!(!potential(&app, &ours.key));
    }

    // A test must not send a report to Threema's real directory: those calling `report_spam` or
    // `send_group_reports` with a suspect have the stand-in below answer. The requests are
    // tested in `directory.rs` as well, with the phase 1 live test.

    #[tokio::test(flavor = "current_thread")]
    async fn after_a_report_the_chat_stays_unless_asked_otherwise() {
        let (_dir, mut app) = app();
        suspect(&app, |_| {});
        message(&app, SUSPECT, CREATED + DAY);
        let (key, conversation) = app.readable_conversation(SUSPECT).unwrap();
        app.after_report(&key, &conversation, SUSPECT, false)
            .unwrap();
        assert!(!potential(&app, SUSPECT), "reported once");
        assert!(
            app.store
                .earliest_incoming_content(SUSPECT)
                .unwrap()
                .is_some()
        );
        assert!(!blocked(&app, SUSPECT));
    }

    fn blocked(app: &App, id: &str) -> bool {
        app.app_settings()
            .unwrap()
            .blocked_contacts
            .iter()
            .any(|other| other == id)
    }

    #[tokio::test(flavor = "current_thread")]
    async fn block_and_delete_a_contacts_chat() {
        let (_dir, mut app) = app();
        suspect(&app, |_| {});
        message(&app, SUSPECT, CREATED + DAY);
        let (key, conversation) = app.readable_conversation(SUSPECT).unwrap();
        app.after_report(&key, &conversation, SUSPECT, true)
            .unwrap();
        assert!(blocked(&app, SUSPECT));
        assert_eq!(app.store.earliest_incoming_content(SUSPECT).unwrap(), None);
    }

    #[tokio::test(flavor = "current_thread")]
    async fn block_and_delete_a_group_blocks_its_creator_and_removes_it() {
        tokio::task::LocalSet::new()
            .run_until(async {
                let (_dir, mut app) = app();
                suspect(&app, |_| {});
                let theirs = group(SUSPECT, false);
                app.store.upsert_group(&theirs).unwrap();
                message(&app, &theirs.key, CREATED + DAY);
                let (key, conversation) = app.readable_conversation(&theirs.key).unwrap();
                app.after_report(&key, &conversation, SUSPECT, true)
                    .unwrap();
                assert!(blocked(&app, SUSPECT));
                assert!(app.store.group(&theirs.key).unwrap().is_none());
                assert_eq!(app.store.earliest_incoming_content(&key).unwrap(), None);
                // They still hear that we left, or they answer with the group again.
                let queued = app.store.outbox().unwrap();
                assert_eq!(queued.len(), 1);
                assert_eq!(queued[0].kind, crate::groups::OUTBOX_GROUP_LEAVE);
                assert_eq!(
                    app.store
                        .pending_receivers(&theirs.key, queued[0].message_id)
                        .unwrap(),
                    theirs.members
                );
            })
            .await;
    }

    /// The directory's stand-in: phase 1 gets a challenge, the answered phase 2 `phase2`. Returns
    /// the bodies of the requests in the order they came.
    fn directory(
        phase2: fn() -> libthreema::https::HttpsResult,
    ) -> (
        std::rc::Rc<std::cell::RefCell<Vec<Value>>>,
        crate::https::TestServersInPlace,
    ) {
        use data_encoding::BASE64;
        use libthreema::https::HttpsResponse;

        let seen = std::rc::Rc::new(std::cell::RefCell::new(Vec::new()));
        let seeing = std::rc::Rc::clone(&seen);
        let guard = crate::https::answer_requests_with(move |request| {
            let body: Value = serde_json::from_slice(&request.body).unwrap();
            assert!(request.url.ends_with("/identity/report_group"));
            let answered = body.get("response").is_some();
            seeing.borrow_mut().push(body);
            Box::pin(std::future::ready(if answered {
                phase2()
            } else {
                Ok(HttpsResponse {
                    status: 200,
                    body: serde_json::to_vec(&json!({
                        "token": BASE64.encode(b"a token"),
                        "tokenRespKeyPub": BASE64.encode(&[11; 32]),
                    }))
                    .unwrap(),
                })
            }))
        });
        (seen, guard)
    }

    fn success() -> libthreema::https::HttpsResult {
        Ok(libthreema::https::HttpsResponse {
            status: 200,
            body: br#"{"status":"success"}"#.to_vec(),
        })
    }

    fn a_reported_group(app: &App) -> Group {
        suspect(app, |c| {
            c.acquaintance_level = AcquaintanceLevel::GroupOrDeleted;
            c.nickname = Some("Offers".into());
        });
        let theirs = group(SUSPECT, false);
        app.store.upsert_group(&theirs).unwrap();
        message(app, &theirs.key, CREATED + DAY);
        theirs
    }

    // Android 6.6.0: blocking and deleting happen at once, the report waits in the store and goes
    // in the background.
    #[tokio::test(flavor = "current_thread")]
    async fn a_groups_report_waits_and_blocking_and_deleting_dont() {
        tokio::task::LocalSet::new()
            .run_until(async {
                let (_dir, mut app) = app();
                let theirs = a_reported_group(&app);
                let (seen, _directory) = directory(success);
                app.report_spam(&theirs.key, true).await.unwrap();

                // Before the report went anywhere.
                assert!(seen.borrow().is_empty());
                assert!(blocked(&app, SUSPECT));
                assert!(app.store.group(&theirs.key).unwrap().is_none());
                assert_eq!(app.pending_group_reports().len(), 1);

                // Then it goes by itself.
                for _ in 0..100 {
                    if app.pending_group_reports().is_empty() {
                        break;
                    }
                    tokio::task::yield_now().await;
                }
                assert!(app.pending_group_reports().is_empty());
                assert_eq!(seen.borrow().len(), 2);
            })
            .await;
    }

    #[tokio::test(flavor = "current_thread")]
    async fn a_groups_report_goes_with_what_it_was_and_then_is_done() {
        tokio::task::LocalSet::new()
            .run_until(async {
                let (_dir, mut app) = app();
                let theirs = a_reported_group(&app);
                app.queue_group_report(PendingGroupReport {
                    creator: SUSPECT.into(),
                    nickname: Some("Offers".into()),
                    group_id: theirs.identity.group_id,
                    members: 2,
                    tries: 0,
                })
                .unwrap();
                // Gone meanwhile: the report has what it needs.
                app.after_report(
                    &theirs.key,
                    &app.readable_conversation(&theirs.key).unwrap().1,
                    SUSPECT,
                    true,
                )
                .unwrap();
                let (seen, _directory) = directory(success);
                app.shared.send_group_reports().await;

                assert!(app.pending_group_reports().is_empty());
                let seen = seen.borrow();
                assert_eq!(seen.len(), 2, "both phases");
                assert_eq!(seen[1]["reporter_identity"], USER);
                assert_eq!(seen[1]["group_creator_identity"], SUSPECT);
                assert_eq!(seen[1]["group_creator_nickname"], "Offers");
                assert_eq!(seen[1]["group_id"], "4660");
                assert_eq!(seen[1]["group_member_count"], 2);
                assert!(seen[1].get("response").is_some());
            })
            .await;
    }

    #[tokio::test(flavor = "current_thread")]
    async fn a_groups_report_is_tried_again_until_it_is_given_up() {
        let (_dir, app) = app();
        let theirs = a_reported_group(&app);
        let report = PendingGroupReport {
            creator: SUSPECT.into(),
            nickname: None,
            group_id: theirs.identity.group_id,
            members: 2,
            tries: 0,
        };
        app.queue_group_report(report.clone()).unwrap();
        let (_seen, _directory) = directory(|| {
            Err(libthreema::https::HttpsError::Unreachable(
                "stand-in is down".into(),
            ))
        });
        for tries in 1..GROUP_REPORT_TRIES {
            app.shared.send_group_reports().await;
            assert_eq!(
                app.pending_group_reports(),
                [PendingGroupReport {
                    tries,
                    ..report.clone()
                }]
            );
        }
        app.shared.send_group_reports().await;
        assert!(app.pending_group_reports().is_empty(), "given up");
    }

    #[tokio::test(flavor = "current_thread")]
    async fn a_groups_report_the_directory_refuses_is_not_sent_again() {
        let (_dir, app) = app();
        let theirs = a_reported_group(&app);
        app.queue_group_report(PendingGroupReport {
            creator: SUSPECT.into(),
            nickname: None,
            group_id: theirs.identity.group_id,
            members: 2,
            tries: 0,
        })
        .unwrap();
        let (seen, _directory) = directory(|| {
            Ok(libthreema::https::HttpsResponse {
                status: 404,
                body: br#"{"status":"error","code":"ID_NOT_FOUND"}"#.to_vec(),
            })
        });
        app.shared.send_group_reports().await;
        assert!(app.pending_group_reports().is_empty());
        assert_eq!(seen.borrow().len(), 2);
    }

    #[tokio::test(flavor = "current_thread")]
    async fn only_a_suspect_can_be_reported() {
        let (_dir, mut app) = app();
        suspect(&app, |c| c.first_name = Some("Ann".into()));
        message(&app, SUSPECT, CREATED + DAY);
        assert!(app.report_spam(SUSPECT, false).await.is_err());
    }
}
