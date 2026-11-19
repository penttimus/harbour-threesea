// SPDX-License-Identifier: AGPL-3.0-only
import QtQuick 2.6
import Sailfish.Silica 1.0

// The cover's background: the launcher icon's picture in one colour, as the cover's layout. Three
// waves, each filled down to the bottom so they add up: the first under the count, the second
// carries the list, the third the cover actions. The icon's bubble floats on the first, sunk and
// leaning as on the icon. The two upper waves rise with `level` (the number of rows shown) and
// their shape changes a little with it; the third never moves.
// The colouring is Harpoon's (GPL-3.0, github.com/JuhaniLehtimaeki/Harpoon): the ambience's
// highlight background colour, the layers' strengths, the whole at Theme.opacityLow.
Canvas {
    id: sea

    // 0: nothing listed, the sea at its lowest. Fractions while it moves.
    property real level
    // The waves' middle lines.
    property real firstY
    property real secondY
    property real thirdY
    property real amplitude: Theme.paddingSmall
    property color color: Theme.highlightBackgroundColor
    // The whole picture's strength, of Theme.opacityLow: at full strength the ambience's colour
    // glowed (user 2026-11-19).
    property real intensity: 0.5
    // Where the bubble floats, of the width; the first wave has a crest there at every level.
    readonly property real bubbleAt: 0.8
    readonly property real bubbleWidth: Math.round(width * 0.21)

    // The emulator's renderer blends a texture as if its colours were not multiplied by its
    // alpha yet, which darkens every translucent pixel (docs/emulator.md); there the picture
    // goes out unmultiplied, so the emulator shows what a phone does.
    property bool emulator
    layer.enabled: emulator
    layer.effect: ShaderEffect {
        fragmentShader: "
            varying highp vec2 qt_TexCoord0;
            uniform sampler2D source;
            uniform lowp float qt_Opacity;
            void main() {
                lowp vec4 colour = texture2D(source, qt_TexCoord0);
                gl_FragColor = vec4(colour.rgb / max(colour.a, 0.004), colour.a) * qt_Opacity;
            }"
    }

    // Per level 0..3: the first wave's height (of `amplitude`) and length (periods across the
    // cover), then the second's with where it starts.
    readonly property var _firstHeight: [0.65, 0.75, 1.0, 1.15]
    readonly property var _firstPeriods: [0.9, 1.0, 1.25, 1.5]
    readonly property var _secondHeight: [0.75, 0.75, 0.9, 1.0]
    readonly property var _secondPeriods: [1.25, 1.5, 1.0, 1.25]
    readonly property var _secondPhase: [4.0, 4.0, 1.2, 2.5]

    function _at(list, level) {
        var l = Math.max(0, Math.min(list.length - 1, level))
        var i = Math.floor(l)
        var j = Math.min(list.length - 1, i + 1)
        return list[i] + (list[j] - list[i]) * (l - i)
    }

    function _waveY(wave, t) {
        return wave.y + wave.amp * Math.sin(t * wave.periods * 2 * Math.PI + wave.phase)
    }

    // Everything under the wave, or above it.
    function _trace(ctx, wave, above) {
        var edge = above ? -1 : height + 1
        ctx.beginPath()
        ctx.moveTo(-1, edge)
        for (var i = 0; i <= 64; i++) {
            ctx.lineTo(i / 64 * (width + 2) - 1, _waveY(wave, i / 64))
        }
        ctx.lineTo(width + 1, edge)
        ctx.closePath()
    }

    function _tint(strength) {
        return "rgba(" + Math.round(color.r * 255) + "," + Math.round(color.g * 255) + ","
                + Math.round(color.b * 255) + "," + (strength * Theme.opacityLow * intensity).toFixed(3) + ")"
    }

    // From the top left to the bottom right, like the light on the icon.
    function _fade(ctx, x0, y0, x1, y1, from, to) {
        var gradient = ctx.createLinearGradient(x0, y0, x1, y1)
        gradient.addColorStop(0, _tint(from))
        gradient.addColorStop(1, _tint(to))
        return gradient
    }

    // The icon's bubble (48 x 39 there): round corners, the bottom left one nearly square.
    function _bubble(ctx, w, h) {
        var r = w / 3
        var corner = w / 30
        ctx.moveTo(r, 0)
        ctx.lineTo(w - r, 0)
        ctx.arc(w - r, r, r, -Math.PI / 2, 0, false)
        ctx.lineTo(w, h - r)
        ctx.arc(w - r, h - r, r, 0, Math.PI / 2, false)
        ctx.lineTo(corner, h)
        ctx.arc(corner, h - corner, corner, Math.PI / 2, Math.PI, false)
        ctx.lineTo(0, r)
        ctx.arc(r, r, r, Math.PI, 1.5 * Math.PI, false)
        ctx.closePath()
    }

    onLevelChanged: requestPaint()
    onFirstYChanged: requestPaint()
    onSecondYChanged: requestPaint()
    onThirdYChanged: requestPaint()
    onAmplitudeChanged: requestPaint()
    onColorChanged: requestPaint()
    onIntensityChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        if (width <= 0 || height <= 0) return

        var firstPeriods = _at(_firstPeriods, level)
        var waves = [{
            y: firstY, amp: amplitude * _at(_firstHeight, level), periods: firstPeriods,
            // A crest under the bubble.
            phase: -Math.PI / 2 - bubbleAt * firstPeriods * 2 * Math.PI,
            from: 0.30, to: 0.16
        }, {
            y: secondY, amp: amplitude * _at(_secondHeight, level),
            periods: _at(_secondPeriods, level), phase: _at(_secondPhase, level),
            from: 0.40, to: 0.26
        }, {
            y: thirdY, amp: amplitude, periods: 1.25, phase: 4.4, from: 0.9, to: 0.7
        }]
        for (var i = 0; i < waves.length; i++) {
            var wave = waves[i]
            _trace(ctx, wave, false)
            ctx.fillStyle = _fade(ctx, 0, wave.y - wave.amp, width,
                                  wave.y + wave.amp + 2 * Theme.paddingLarge, wave.from, wave.to)
            ctx.fill()
        }

        // The bubble: nothing of it under the water. On the icon a fifth of its height is
        // sunk at the crest and it leans by 7 degrees.
        var w = bubbleWidth
        var h = w * 39 / 48
        ctx.save()
        _trace(ctx, waves[0], true)
        ctx.clip()
        ctx.translate(width * bubbleAt, _waveY(waves[0], bubbleAt) - 0.295 * h)
        ctx.rotate(-7 * Math.PI / 180)
        ctx.translate(-w / 2, -h / 2)
        ctx.beginPath()
        _bubble(ctx, w, h)
        // The three dots, left open.
        var dots = [0.27, 0.5, 0.73]
        for (var d = 0; d < dots.length; d++) {
            ctx.moveTo(w * dots[d] + w * 0.077, h / 2)
            ctx.arc(w * dots[d], h / 2, w * 0.077, 0, 2 * Math.PI, true)
            ctx.closePath()
        }
        ctx.fillStyle = _fade(ctx, 0, 0, w, h, 0.43, 0.21)
        ctx.fill()
        ctx.restore()
    }
}
