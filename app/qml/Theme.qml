pragma Singleton
// =========================================================================
// Theme - the single source of colour and typography for the whole UI.
//
// Before this, every panel carried its own hard-coded hex literals, which is
// why text contrast was inconsistent and unreadable in places.
//
// The catalogue below is generated, not hand-written, and that is the point:
// every token is checked against the surface it is drawn on before it ships.
// text clears 13:1, secondary text 6.5:1, muted 4.4:1, disabled 3:1, the accent
// 2.9:1, and an accent's own label 4.5:1 against the accent. A palette that
// cannot meet those is adjusted until it does. This is what stops the light
// themes hiding half the interface, which they previously did.
//
// 108 themes in four groups: Dark, Light, High contrast and Colourblind. The
// generator lives in tools/theme-generator and the seeds are hue + chroma
// character + accent hue per theme, so a new palette is three numbers rather
// than seventeen hex values guessed by eye.
//
// Four groups, not the eight it started with. Splitting by palette character -
// core, colour, vivid, paper - is how a designer thinks about colour and not how
// anyone chooses a theme; with a hundred entries it read as one long list with
// headings in it. Dark or light is the question people actually start from, and
// the picker keeps every group but the current one collapsed.
//
// The Colourblind group is named for what it is rather than for the technical
// term, and each of its themes carries "(colourblind)" in its own name so a
// settings row is self-explanatory without the heading above it. Those eight are
// not named optimistically: each one's accent and its positive/warning/danger
// colours are simulated through that deficiency (Vienot / Brettel-Mollon) and
// chosen by farthest-point selection in CIELAB, so the four signals a user has
// to tell apart provably stay apart. Hand-picked hues failed this - a
// "deuteranopia" accent collided with its own warning colour once simulated.
//
// The colours inside a graph are a SEPARATE, persisted setting - see
// native/plot2d/include/ColourVision.h. Someone needs their figures safe
// whichever theme they happen to like, so the two do not move together.
// =========================================================================
import QtQuick

QtObject {
    id: theme

    readonly property var themes: [
        { name: "Obsidian", group: "Dark", light: false, cvd: "", bg: "#101111", surface: "#181a1b", surfaceAlt: "#212224", border: "#36383b", borderStrong: "#505458", text: "#f2f2f3", text2: "#c4c7c9", muted: "#999ea3", disabled: "#747a81", accent: "#669ddb", onAccent: "#090c11", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Graphite", group: "Dark", light: false, cvd: "", bg: "#101011", surface: "#18191b", surfaceAlt: "#212224", border: "#36373b", borderStrong: "#505358", text: "#f2f2f3", text2: "#c4c6c9", muted: "#999ca3", disabled: "#747881", accent: "#66b4db", onAccent: "#090e11", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Slate", group: "Dark", light: false, cvd: "", bg: "#0f1112", surface: "#171a1c", surfaceAlt: "#1e2327", border: "#32393e", borderStrong: "#4a565e", text: "#f1f2f3", text2: "#c1c8cd", muted: "#92a0aa", disabled: "#6c7d89", accent: "#5fbbe3", onAccent: "#090e11", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Charcoal", group: "Dark", light: false, cvd: "", bg: "#111110", surface: "#1b1a18", surfaceAlt: "#242221", border: "#3b3836", borderStrong: "#585450", text: "#f3f2f2", text2: "#c9c7c4", muted: "#a39e99", disabled: "#817a74", accent: "#dba566", onAccent: "#110d09", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Basalt", group: "Dark", light: false, cvd: "", bg: "#101011", surface: "#19181b", surfaceAlt: "#212124", border: "#36363b", borderStrong: "#515058", text: "#f2f2f3", text2: "#c5c4c9", muted: "#9b99a3", disabled: "#767481", accent: "#9166db", onAccent: "#0c0911", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Anthracite", group: "Dark", light: false, cvd: "", bg: "#101111", surface: "#181a1b", surfaceAlt: "#212324", border: "#36393b", borderStrong: "#505658", text: "#f2f3f3", text2: "#c4c8c9", muted: "#99a1a3", disabled: "#747d81", accent: "#66cfdb", onAccent: "#091011", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Ironwood", group: "Dark", light: false, cvd: "", bg: "#12100f", surface: "#1c1917", surfaceAlt: "#27221e", border: "#3e3732", borderStrong: "#5e524a", text: "#f3f2f1", text2: "#cdc6c1", muted: "#aa9c92", disabled: "#89786c", accent: "#e3865f", onAccent: "#110b09", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Gunmetal", group: "Dark", light: false, cvd: "", bg: "#0f1012", surface: "#17191c", surfaceAlt: "#1e2227", border: "#32373e", borderStrong: "#4a525e", text: "#f1f2f3", text2: "#c1c6cd", muted: "#929caa", disabled: "#6c7889", accent: "#5f80e3", onAccent: "#090b11", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Onyx", group: "Dark", light: false, cvd: "", bg: "#111011", surface: "#1a181b", surfaceAlt: "#222124", border: "#38363b", borderStrong: "#545058", text: "#f2f2f3", text2: "#c7c4c9", muted: "#9e99a3", disabled: "#7a7481", accent: "#c466db", onAccent: "#0f0911", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Steel", group: "Dark", light: false, cvd: "", bg: "#0f1112", surface: "#171a1c", surfaceAlt: "#1e2327", border: "#32393e", borderStrong: "#4a555e", text: "#f1f2f3", text2: "#c1c8cd", muted: "#929faa", disabled: "#6c7c89", accent: "#5face3", onAccent: "#090d11", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Pitch", group: "Dark", light: false, cvd: "", bg: "#111010", surface: "#1b1818", surfaceAlt: "#242121", border: "#3b3636", borderStrong: "#585050", text: "#f3f2f2", text2: "#c9c4c4", muted: "#a39999", disabled: "#817474", accent: "#db667a", onAccent: "#11090a", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Umber", group: "Dark", light: false, cvd: "", bg: "#12100f", surface: "#1c1917", surfaceAlt: "#27221e", border: "#3e3832", borderStrong: "#5e534a", text: "#f3f2f1", text2: "#cdc6c1", muted: "#aa9d92", disabled: "#89796c", accent: "#e3b75f", onAccent: "#110e09", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Deep Space", group: "Dark", light: false, cvd: "", bg: "#0f0f12", surface: "#17171c", surfaceAlt: "#1e1f27", border: "#32333e", borderStrong: "#4a4c5e", text: "#f1f1f3", text2: "#c1c2cd", muted: "#9294aa", disabled: "#6c6e89", accent: "#705fe3", onAccent: "#ffffff", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Carbon", group: "Dark", light: false, cvd: "", bg: "#101111", surface: "#181b1a", surfaceAlt: "#212423", border: "#363b39", borderStrong: "#505856", text: "#f2f3f2", text2: "#c4c9c8", muted: "#99a3a0", disabled: "#74817c", accent: "#66dbc4", onAccent: "#09110f", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Shale", group: "Dark", light: false, cvd: "", bg: "#0f1212", surface: "#171c1c", surfaceAlt: "#1e2627", border: "#323d3e", borderStrong: "#4a5d5e", text: "#f1f3f3", text2: "#c1cccd", muted: "#92a8aa", disabled: "#6c8789", accent: "#5fe3de", onAccent: "#091110", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Tungsten", group: "Dark", light: false, cvd: "", bg: "#111110", surface: "#1b1a18", surfaceAlt: "#242321", border: "#3b3936", borderStrong: "#585650", text: "#f3f3f2", text2: "#c9c8c4", muted: "#a3a199", disabled: "#817d74", accent: "#dbc866", onAccent: "#110f09", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Nightshade", group: "Dark", light: false, cvd: "", bg: "#110f12", surface: "#1b171c", surfaceAlt: "#241e27", border: "#3b323e", borderStrong: "#594a5e", text: "#f3f1f3", text2: "#cac1cd", muted: "#a492aa", disabled: "#826c89", accent: "#e35fe3", onAccent: "#110911", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Flint", group: "Dark", light: false, cvd: "", bg: "#101111", surface: "#181b1a", surfaceAlt: "#212422", border: "#363b38", borderStrong: "#505854", text: "#f2f3f2", text2: "#c4c9c7", muted: "#99a39e", disabled: "#74817a", accent: "#66db8d", onAccent: "#09110b", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Cinder", group: "Dark", light: false, cvd: "", bg: "#111010", surface: "#1b1918", surfaceAlt: "#242121", border: "#3b3736", borderStrong: "#585250", text: "#f3f2f2", text2: "#c9c5c4", muted: "#a39b99", disabled: "#817774", accent: "#db7266", onAccent: "#110a09", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Void", group: "Dark", light: false, cvd: "", bg: "#101011", surface: "#18181b", surfaceAlt: "#212124", border: "#36363b", borderStrong: "#505058", text: "#f2f2f3", text2: "#c4c4c9", muted: "#9999a3", disabled: "#747481", accent: "#6676db", onAccent: "#090a11", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Deep Sea", group: "Dark", light: false, cvd: "", bg: "#0d1214", surface: "#141d1f", surfaceAlt: "#1b272a", border: "#2d3f43", borderStrong: "#425f67", text: "#f0f4f4", text2: "#bcced2", muted: "#89abb3", disabled: "#5f8b95", accent: "#57eae5", onAccent: "#091110", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Abyss", group: "Dark", light: false, cvd: "", bg: "#0d1114", surface: "#141a1f", surfaceAlt: "#1b242a", border: "#2d3a43", borderStrong: "#425767", text: "#f0f3f4", text2: "#bcc9d2", muted: "#89a2b3", disabled: "#5f7f95", accent: "#57d2ea", onAccent: "#090f11", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Kelp Forest", group: "Dark", light: false, cvd: "", bg: "#0d1412", surface: "#141f1c", surfaceAlt: "#1b2a25", border: "#2d433d", borderStrong: "#42675c", text: "#f0f4f3", text2: "#bcd2cb", muted: "#89b3a7", disabled: "#5f9585", accent: "#57eaa1", onAccent: "#09110d", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Midnight Reef", group: "Dark", light: false, cvd: "", bg: "#0d1214", surface: "#141b1f", surfaceAlt: "#1b252a", border: "#2d3c43", borderStrong: "#425a67", text: "#f0f3f4", text2: "#bccbd2", muted: "#89a5b3", disabled: "#5f8395", accent: "#ea57b9", onAccent: "#11090e", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Plum", group: "Dark", light: false, cvd: "", bg: "#130d14", surface: "#1d141f", surfaceAlt: "#271b2a", border: "#402d43", borderStrong: "#604267", text: "#f4f0f4", text2: "#cebcd2", muted: "#ac89b3", disabled: "#8c5f95", accent: "#ea57cd", onAccent: "#11090f", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Mulberry", group: "Dark", light: false, cvd: "", bg: "#140d12", surface: "#1f141b", surfaceAlt: "#2a1b25", border: "#432d3c", borderStrong: "#67425a", text: "#f4f0f3", text2: "#d2bccb", muted: "#b389a5", disabled: "#955f83", accent: "#ea5794", onAccent: "#11090c", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Pine", group: "Dark", light: false, cvd: "", bg: "#0d1411", surface: "#141f1a", surfaceAlt: "#1b2a22", border: "#2d4338", borderStrong: "#426754", text: "#f0f4f2", text2: "#bcd2c7", muted: "#89b39e", disabled: "#5f957a", accent: "#57ea83", onAccent: "#09110b", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Moss", group: "Dark", light: false, cvd: "", bg: "#10140d", surface: "#181f14", surfaceAlt: "#202a1b", border: "#34432d", borderStrong: "#4e6742", text: "#f2f4f0", text2: "#c3d2bc", muted: "#97b389", disabled: "#71955f", accent: "#a6ea57", onAccent: "#0d1109", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Ember", group: "Dark", light: false, cvd: "", bg: "#14100d", surface: "#1f1814", surfaceAlt: "#2a201b", border: "#43352d", borderStrong: "#674f42", text: "#f4f2f0", text2: "#d2c4bc", muted: "#b39889", disabled: "#95735f", accent: "#eaaa57", onAccent: "#110d09", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Rust", group: "Dark", light: false, cvd: "", bg: "#140f0d", surface: "#1f1714", surfaceAlt: "#2a1e1b", border: "#43322d", borderStrong: "#674a42", text: "#f4f1f0", text2: "#d2c1bc", muted: "#b39389", disabled: "#956c5f", accent: "#ea9757", onAccent: "#110c09", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Indigo", group: "Dark", light: false, cvd: "", bg: "#0e0d14", surface: "#16141f", surfaceAlt: "#1d1b2a", border: "#302d43", borderStrong: "#474267", text: "#f1f0f4", text2: "#bfbcd2", muted: "#8e89b3", disabled: "#675f95", accent: "#8357ea", onAccent: "#ffffff", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Cobalt", group: "Dark", light: false, cvd: "", bg: "#0d0f14", surface: "#14171f", surfaceAlt: "#1b1f2a", border: "#2d3443", borderStrong: "#424d67", text: "#f0f1f4", text2: "#bcc2d2", muted: "#8996b3", disabled: "#5f7095", accent: "#579cea", onAccent: "#090c11", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Aubergine", group: "Dark", light: false, cvd: "", bg: "#120d14", surface: "#1c141f", surfaceAlt: "#251b2a", border: "#3d2d43", borderStrong: "#5c4267", text: "#f3f0f4", text2: "#cbbcd2", muted: "#a789b3", disabled: "#855f95", accent: "#e057ea", onAccent: "#100911", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Teal Depths", group: "Dark", light: false, cvd: "", bg: "#0d1314", surface: "#141e1f", surfaceAlt: "#1b292a", border: "#2d4243", borderStrong: "#426567", text: "#f0f4f4", text2: "#bcd1d2", muted: "#89b2b3", disabled: "#5f9495", accent: "#57ead2", onAccent: "#09110f", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Blackcurrant", group: "Dark", light: false, cvd: "", bg: "#140d13", surface: "#1f141d", surfaceAlt: "#2a1b27", border: "#432d40", borderStrong: "#674260", text: "#f4f0f4", text2: "#d2bcce", muted: "#b389ac", disabled: "#955f8c", accent: "#ea57a1", onAccent: "#11090d", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Juniper", group: "Dark", light: false, cvd: "", bg: "#0d1412", surface: "#141f1d", surfaceAlt: "#1b2a27", border: "#2d433f", borderStrong: "#42675f", text: "#f0f4f4", text2: "#bcd2ce", muted: "#89b3ab", disabled: "#5f958b", accent: "#57eaaf", onAccent: "#09110e", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Bordeaux", group: "Dark", light: false, cvd: "", bg: "#140d0f", surface: "#1f1416", surfaceAlt: "#2a1b1e", border: "#432d31", borderStrong: "#674249", text: "#f4f0f1", text2: "#d2bcc0", muted: "#b38991", disabled: "#955f6a", accent: "#ea6b57", onAccent: "#110a09", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Petrol", group: "Dark", light: false, cvd: "", bg: "#0d1214", surface: "#141c1f", surfaceAlt: "#1b262a", border: "#2d3d43", borderStrong: "#425d67", text: "#f0f3f4", text2: "#bcccd2", muted: "#89a8b3", disabled: "#5f8795", accent: "#57afea", onAccent: "#090e11", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Olive Night", group: "Dark", light: false, cvd: "", bg: "#12140d", surface: "#1c1f14", surfaceAlt: "#262a1b", border: "#3e432d", borderStrong: "#5d6742", text: "#f3f4f0", text2: "#ccd2bc", muted: "#a9b389", disabled: "#88955f", accent: "#e5ea57", onAccent: "#101109", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Sapphire", group: "Dark", light: false, cvd: "", bg: "#0d0f14", surface: "#14161f", surfaceAlt: "#1b1e2a", border: "#2d3143", borderStrong: "#424967", text: "#f0f1f4", text2: "#bcc0d2", muted: "#8991b3", disabled: "#5f6a95", accent: "#578dea", onAccent: "#090c11", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Tidepool", group: "Dark", light: false, cvd: "", bg: "#0d1413", surface: "#141f1e", surfaceAlt: "#1b2a29", border: "#2d4342", borderStrong: "#426764", text: "#f0f4f4", text2: "#bcd2d1", muted: "#89b3b1", disabled: "#5f9592", accent: "#57d7ea", onAccent: "#091011", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Cocoa", group: "Dark", light: false, cvd: "", bg: "#14100d", surface: "#1f1814", surfaceAlt: "#2a201b", border: "#43342d", borderStrong: "#674e42", text: "#f4f2f0", text2: "#d2c3bc", muted: "#b39789", disabled: "#95715f", accent: "#eab457", onAccent: "#110e09", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Neon Reef", group: "Dark", light: false, cvd: "", bg: "#0c1415", surface: "#122021", surfaceAlt: "#172c2e", border: "#274649", borderStrong: "#386b71", text: "#f0f4f5", text2: "#bbd1d3", muted: "#87b1b5", disabled: "#5d9298", accent: "#4af7e0", onAccent: "#091110", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Synthwave", group: "Dark", light: false, cvd: "", bg: "#150c15", surface: "#201221", surfaceAlt: "#2c172e", border: "#462749", borderStrong: "#6c3871", text: "#f4f0f5", text2: "#d1bbd3", muted: "#b187b5", disabled: "#935d98", accent: "#f74ab8", onAccent: "#11090e", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Cyberlime", group: "Dark", light: false, cvd: "", bg: "#0c150f", surface: "#122117", surfaceAlt: "#172e1f", border: "#274932", borderStrong: "#38714b", text: "#f0f5f1", text2: "#bbd3c3", muted: "#87b596", disabled: "#5d9871", accent: "#84f74a", onAccent: "#0b1109", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Hot Coral", group: "Dark", light: false, cvd: "", bg: "#150c0e", surface: "#211216", surfaceAlt: "#2e171d", border: "#492730", borderStrong: "#713846", text: "#f5f0f1", text2: "#d3bbc1", muted: "#b58792", disabled: "#985d6c", accent: "#f7724a", onAccent: "#110b09", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Electric Iris", group: "Dark", light: false, cvd: "", bg: "#0f0c15", surface: "#161221", surfaceAlt: "#1e172e", border: "#312749", borderStrong: "#493871", text: "#f1f0f5", text2: "#c2bbd3", muted: "#9587b5", disabled: "#6f5d98", accent: "#b24af7", onAccent: "#0e0911", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Acid Rain", group: "Dark", light: false, cvd: "", bg: "#11150c", surface: "#1a2112", surfaceAlt: "#232e17", border: "#394927", borderStrong: "#567138", text: "#f2f5f0", text2: "#c8d3bb", muted: "#a0b587", disabled: "#7c985d", accent: "#d5f74a", onAccent: "#0f1109", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Vapour", group: "Dark", light: false, cvd: "", bg: "#150c12", surface: "#21121c", surfaceAlt: "#2e1726", border: "#49273e", borderStrong: "#71385e", text: "#f5f0f3", text2: "#d3bbcb", muted: "#b587a6", disabled: "#985d84", accent: "#4ad5f7", onAccent: "#090f11", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Laser Grape", group: "Dark", light: false, cvd: "", bg: "#110c15", surface: "#1a1221", surfaceAlt: "#23172e", border: "#392749", borderStrong: "#563871", text: "#f2f0f5", text2: "#c8bbd3", muted: "#a087b5", disabled: "#7c5d98", accent: "#f74af7", onAccent: "#110911", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Magma", group: "Dark", light: false, cvd: "", bg: "#150d0c", surface: "#211412", surfaceAlt: "#2e1b17", border: "#492d27", borderStrong: "#714138", text: "#f5f1f0", text2: "#d3bfbb", muted: "#b58f87", disabled: "#98675d", accent: "#f7a64a", onAccent: "#110d09", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Ultramarine", group: "Dark", light: false, cvd: "", bg: "#0c0d15", surface: "#121421", surfaceAlt: "#171a2e", border: "#272c49", borderStrong: "#383f71", text: "#f0f1f5", text2: "#bbbed3", muted: "#878db5", disabled: "#5d6598", accent: "#4aaff7", onAccent: "#090d11", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Toxic Teal", group: "Dark", light: false, cvd: "", bg: "#0c1514", surface: "#122120", surfaceAlt: "#172e2c", border: "#274946", borderStrong: "#38716b", text: "#f0f5f4", text2: "#bbd3d1", muted: "#87b5b1", disabled: "#5d9892", accent: "#4af7be", onAccent: "#09110e", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Fuchsia Dusk", group: "Dark", light: false, cvd: "", bg: "#150c11", surface: "#21121a", surfaceAlt: "#2e1723", border: "#492739", borderStrong: "#713856", text: "#f5f0f2", text2: "#d3bbc8", muted: "#b587a0", disabled: "#985d7c", accent: "#f74a6d", onAccent: "#11090a", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Signal Amber", group: "Dark", light: false, cvd: "", bg: "#15120c", surface: "#211c12", surfaceAlt: "#2e2617", border: "#493e27", borderStrong: "#715e38", text: "#f5f3f0", text2: "#d3cbbb", muted: "#b5a687", disabled: "#98845d", accent: "#f7cf4a", onAccent: "#110f09", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Deep Neon", group: "Dark", light: false, cvd: "", bg: "#0c1115", surface: "#121921", surfaceAlt: "#17222e", border: "#273849", borderStrong: "#385471", text: "#f0f2f5", text2: "#bbc7d3", muted: "#879eb5", disabled: "#5d7a98", accent: "#d54af7", onAccent: "#0f0911", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Paper", group: "Light", light: true, cvd: "", bg: "#f6f6f6", surface: "#ffffff", surfaceAlt: "#eeeeef", border: "#d8dadc", borderStrong: "#b4b8bb", text: "#1d1f20", text2: "#494c50", muted: "#6b7076", disabled: "#8c9197", accent: "#2967ae", onAccent: "#f9fafb", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Snow", group: "Light", light: true, cvd: "", bg: "#f6f6f6", surface: "#ffffff", surfaceAlt: "#eeeeef", border: "#d8d9dc", borderStrong: "#b4b6bb", text: "#1d1e20", text2: "#494b50", muted: "#6b6e76", disabled: "#8c9097", accent: "#2976ae", onAccent: "#f9fafb", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Chalk", group: "Light", light: true, cvd: "", bg: "#f6f6f6", surface: "#ffffff", surfaceAlt: "#eeefef", border: "#d8dbdc", borderStrong: "#b4b9bb", text: "#1d1f20", text2: "#494e50", muted: "#6b7276", disabled: "#8c9397", accent: "#298aae", onAccent: "#090f11", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Porcelain", group: "Light", light: true, cvd: "", bg: "#f5f7f7", surface: "#ffffff", surfaceAlt: "#eceff0", border: "#d6dcde", borderStrong: "#afbcc0", text: "#1c2022", text2: "#445155", muted: "#63777e", disabled: "#80949b", accent: "#2084b6", onAccent: "#090e11", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Ash", group: "Light", light: true, cvd: "", bg: "#f6f6f6", surface: "#ffffff", surfaceAlt: "#eeeeef", border: "#d8dadc", borderStrong: "#b4b7bb", text: "#1d1e20", text2: "#494c50", muted: "#6b6f76", disabled: "#8c9097", accent: "#2951ae", onAccent: "#f9fafb", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Cotton", group: "Light", light: true, cvd: "", bg: "#f6f6f6", surface: "#ffffff", surfaceAlt: "#efeeee", border: "#dcdad8", borderStrong: "#bbb8b4", text: "#201f1d", text2: "#504c49", muted: "#76706b", disabled: "#97918c", accent: "#ae6729", onAccent: "#000000", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Bone", group: "Light", light: true, cvd: "", bg: "#f6f6f6", surface: "#ffffff", surfaceAlt: "#efefee", border: "#dcdbd8", borderStrong: "#bbb9b4", text: "#201f1d", text2: "#504e49", muted: "#76726b", disabled: "#938f88", accent: "#ae7829", onAccent: "#110e09", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Marble", group: "Light", light: true, cvd: "", bg: "#f5f5f7", surface: "#ffffff", surfaceAlt: "#ecedf0", border: "#d6d7de", borderStrong: "#afb2c0", text: "#1c1d22", text2: "#444755", muted: "#63677e", disabled: "#8d91a5", accent: "#2025b6", onAccent: "#f9f9fb", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Quartz", group: "Light", light: true, cvd: "", bg: "#f6f6f6", surface: "#ffffff", surfaceAlt: "#eeeeef", border: "#dad8dc", borderStrong: "#b8b4bb", text: "#1f1d20", text2: "#4c4950", muted: "#706b76", disabled: "#95909a", accent: "#8629ae", onAccent: "#faf9fb", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Cloud", group: "Light", light: true, cvd: "", bg: "#f5f6f7", surface: "#ffffff", surfaceAlt: "#eceff0", border: "#d6dbde", borderStrong: "#afb9c0", text: "#1c1f22", text2: "#444e55", muted: "#63727e", disabled: "#84949f", accent: "#209db6", onAccent: "#090f11", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Frost", group: "Light", light: true, cvd: "", bg: "#f5f7f7", surface: "#ffffff", surfaceAlt: "#ecf0f0", border: "#d6ddde", borderStrong: "#afbdc0", text: "#1c2122", text2: "#445255", muted: "#5f7579", disabled: "#7c9398", accent: "#1ea4a9", onAccent: "#091011", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Linen", group: "Light", light: true, cvd: "", bg: "#f7f6f5", surface: "#ffffff", surfaceAlt: "#f0efec", border: "#dedbd6", borderStrong: "#c0b9af", text: "#221f1c", text2: "#554e44", muted: "#7e7263", disabled: "#9b9080", accent: "#b66120", onAccent: "#000000", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Pearl", group: "Light", light: true, cvd: "", bg: "#f6f6f6", surface: "#ffffff", surfaceAlt: "#eeeeef", border: "#d9d8dc", borderStrong: "#b5b4bb", text: "#1e1d20", text2: "#4a4950", muted: "#6c6b76", disabled: "#92909a", accent: "#5129ae", onAccent: "#faf9fb", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Mist", group: "Light", light: true, cvd: "", bg: "#f5f7f7", surface: "#ffffff", surfaceAlt: "#ecf0ef", border: "#d6dedc", borderStrong: "#afc0bc", text: "#1c2220", text2: "#445551", muted: "#5f7973", disabled: "#7c9891", accent: "#1ea976", onAccent: "#09110e", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Limestone", group: "Light", light: true, cvd: "", bg: "#f6f6f6", surface: "#ffffff", surfaceAlt: "#efefee", border: "#dcdbd8", borderStrong: "#bbbab4", text: "#201f1d", text2: "#504f49", muted: "#76746b", disabled: "#939188", accent: "#ae8a29", onAccent: "#110f09", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Silver", group: "Light", light: true, cvd: "", bg: "#f6f6f6", surface: "#ffffff", surfaceAlt: "#eeeeef", border: "#d8dadc", borderStrong: "#b4b8bb", text: "#1d1f20", text2: "#494d50", muted: "#6b7176", disabled: "#8c9297", accent: "#2962ae", onAccent: "#f9fafb", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Lagoon", group: "Light", light: true, cvd: "", bg: "#f4f7f8", surface: "#ffffff", surfaceAlt: "#ebf1f2", border: "#d3dfe1", borderStrong: "#a8c3c7", text: "#192324", text2: "#3d585c", muted: "#527980", disabled: "#6b99a1", accent: "#14a3a3", onAccent: "#091111", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Meadow", group: "Light", light: true, cvd: "", bg: "#f4f8f5", surface: "#ffffff", surfaceAlt: "#ebf2ec", border: "#d3e1d5", borderStrong: "#a8c7ad", text: "#19241b", text2: "#3d5c42", muted: "#528059", disabled: "#679e70", accent: "#1aaa15", onAccent: "#091109", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Blossom", group: "Light", light: true, cvd: "", bg: "#f8f4f6", surface: "#ffffff", surfaceAlt: "#f2ebee", border: "#e1d3d9", borderStrong: "#c7a8b5", text: "#24191e", text2: "#5c3d4a", muted: "#89586c", disabled: "#af8395", accent: "#bf1841", onAccent: "#fbf9fa", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Wisteria", group: "Light", light: true, cvd: "", bg: "#f7f4f8", surface: "#ffffff", surfaceAlt: "#f0ebf2", border: "#dcd3e1", borderStrong: "#bda8c7", text: "#201924", text2: "#523d5c", muted: "#785889", disabled: "#a083af", accent: "#a818bf", onAccent: "#faf9fb", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Sandbar", group: "Light", light: true, cvd: "", bg: "#f8f7f4", surface: "#ffffff", surfaceAlt: "#f2efeb", border: "#e1dcd3", borderStrong: "#c7bca8", text: "#242019", text2: "#5c513d", muted: "#847355", disabled: "#a18d6b", accent: "#bf6b18", onAccent: "#110d09", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Cornflower", group: "Light", light: true, cvd: "", bg: "#f4f5f8", surface: "#ffffff", surfaceAlt: "#ebedf2", border: "#d3d7e1", borderStrong: "#a8b1c7", text: "#191c24", text2: "#3d465c", muted: "#586689", disabled: "#8390af", accent: "#1860bf", onAccent: "#f9fafb", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Seafoam", group: "Light", light: true, cvd: "", bg: "#f4f8f7", surface: "#ffffff", surfaceAlt: "#ebf2f0", border: "#d3e1de", borderStrong: "#a8c7bf", text: "#192421", text2: "#3d5c54", muted: "#4f7b70", disabled: "#629a8c", accent: "#15aa65", onAccent: "#09110d", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Apricot", group: "Light", light: true, cvd: "", bg: "#f8f6f4", surface: "#ffffff", surfaceAlt: "#f2eeeb", border: "#e1d9d3", borderStrong: "#c7b4a8", text: "#241e19", text2: "#5c493d", muted: "#896b58", disabled: "#a98d79", accent: "#bf7c18", onAccent: "#110e09", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Lilac", group: "Light", light: true, cvd: "", bg: "#f7f4f8", surface: "#ffffff", surfaceAlt: "#f1ebf2", border: "#dfd3e1", borderStrong: "#c2a8c7", text: "#221924", text2: "#573d5c", muted: "#815889", disabled: "#a883af", accent: "#bf18b9", onAccent: "#fbf9fb", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Harbour", group: "Light", light: true, cvd: "", bg: "#f4f6f8", surface: "#ffffff", surfaceAlt: "#ebeff2", border: "#d3dbe1", borderStrong: "#a8bac7", text: "#191f24", text2: "#3d4f5c", muted: "#587489", disabled: "#7995a9", accent: "#1892bf", onAccent: "#090f11", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Sage", group: "Light", light: true, cvd: "", bg: "#f5f8f4", surface: "#ffffff", surfaceAlt: "#edf2eb", border: "#d6e1d3", borderStrong: "#b0c7a8", text: "#1c2419", text2: "#455c3d", muted: "#5a7b4f", disabled: "#709a62", accent: "#52a314", onAccent: "#0c1109", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Coral Reef", group: "Light", light: true, cvd: "", bg: "#f8f4f5", surface: "#ffffff", surfaceAlt: "#f2ebec", border: "#e1d3d5", borderStrong: "#c7a8ad", text: "#24191b", text2: "#5c3d42", muted: "#895860", disabled: "#af838a", accent: "#bf3318", onAccent: "#fbf9f9", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Periwinkle", group: "Light", light: true, cvd: "", bg: "#f4f4f8", surface: "#ffffff", surfaceAlt: "#ebebf2", border: "#d3d3e1", borderStrong: "#a8a8c7", text: "#191924", text2: "#3d3d5c", muted: "#585889", disabled: "#8383af", accent: "#3318bf", onAccent: "#f9f9fb", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Citrus", group: "Light", light: true, cvd: "", bg: "#f8f8f4", surface: "#ffffff", surfaceAlt: "#f2f2eb", border: "#e1e1d3", borderStrong: "#c7c6a8", text: "#242419", text2: "#57563a", muted: "#76754c", disabled: "#95945f", accent: "#b18d16", onAccent: "#110f09", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Parchment", group: "Light", light: true, cvd: "", bg: "#f7f6f5", surface: "#ffffff", surfaceAlt: "#f0efec", border: "#dedcd6", borderStrong: "#c0bbaf", text: "#22201c", text2: "#555044", muted: "#79725f", disabled: "#988f7c", accent: "#b66b20", onAccent: "#110d09", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Manuscript", group: "Light", light: true, cvd: "", bg: "#f8f7f4", surface: "#ffffff", surfaceAlt: "#f2efeb", border: "#e1dcd3", borderStrong: "#c7bca8", text: "#242019", text2: "#5c513d", muted: "#847355", disabled: "#a18d6b", accent: "#bf5518", onAccent: "#ffffff", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Sepia", group: "Light", light: true, cvd: "", bg: "#f8f6f4", surface: "#ffffff", surfaceAlt: "#f2eeeb", border: "#e1dad3", borderStrong: "#c7b7a8", text: "#241e19", text2: "#5c4b3d", muted: "#896f58", disabled: "#a68c75", accent: "#bf4a18", onAccent: "#fbfaf9", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Ivory", group: "Light", light: true, cvd: "", bg: "#f7f7f5", surface: "#ffffff", surfaceAlt: "#f0f0ec", border: "#dedcd6", borderStrong: "#c0bdaf", text: "#22201c", text2: "#555144", muted: "#79745f", disabled: "#98927c", accent: "#b68420", onAccent: "#110e09", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Papyrus", group: "Light", light: true, cvd: "", bg: "#f8f6f4", surface: "#ffffff", surfaceAlt: "#f2efeb", border: "#e1dbd3", borderStrong: "#c7bba8", text: "#242019", text2: "#5c503d", muted: "#847155", disabled: "#a48f70", accent: "#14a3a3", onAccent: "#091111", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Vellum", group: "Light", light: true, cvd: "", bg: "#f7f6f5", surface: "#ffffff", surfaceAlt: "#f0efec", border: "#dedcd6", borderStrong: "#c0bcaf", text: "#22201c", text2: "#555044", muted: "#79725f", disabled: "#98907c", accent: "#2084b6", onAccent: "#090e11", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Newsprint", group: "Light", light: true, cvd: "", bg: "#f6f6f6", surface: "#ffffff", surfaceAlt: "#efefee", border: "#dcdbd8", borderStrong: "#bbb9b4", text: "#201f1d", text2: "#504e49", muted: "#76736b", disabled: "#939088", accent: "#ae2929", onAccent: "#fbf9f9", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Tea Stain", group: "Light", light: true, cvd: "", bg: "#f8f6f4", surface: "#ffffff", surfaceAlt: "#f2eeeb", border: "#e1d9d3", borderStrong: "#c7b6a8", text: "#241e19", text2: "#5c4a3d", muted: "#896d58", disabled: "#a68a75", accent: "#15aa60", onAccent: "#09110d", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Contrast Dark", group: "High contrast", light: false, cvd: "", bg: "#0b0b0c", surface: "#121314", surfaceAlt: "#191b1c", border: "#36383b", borderStrong: "#505458", text: "#f6f6f6", text2: "#c8cbcd", muted: "#9da2a7", disabled: "#787e85", accent: "#66b4db", onAccent: "#090e11", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Contrast Light", group: "High contrast", light: true, cvd: "", bg: "#fbfbfb", surface: "#ffffff", surfaceAlt: "#f3f4f4", border: "#d8dadc", borderStrong: "#b4b8bb", text: "#1a1b1c", text2: "#37393c", muted: "#55595e", disabled: "#71777d", accent: "#2962ae", onAccent: "#f9fafb", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Contrast Amber Dark", group: "High contrast", light: false, cvd: "", bg: "#0c0c0b", surface: "#141312", surfaceAlt: "#1c1b19", border: "#3b3936", borderStrong: "#585650", text: "#f6f6f6", text2: "#c9c8c4", muted: "#a3a099", disabled: "#858078", accent: "#dbbc66", onAccent: "#110f09", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Contrast Sea Dark", group: "High contrast", light: false, cvd: "", bg: "#0b0c0c", surface: "#121414", surfaceAlt: "#191c1c", border: "#363a3b", borderStrong: "#505758", text: "#f6f6f6", text2: "#c4c9c9", muted: "#99a1a3", disabled: "#788285", accent: "#66d3db", onAccent: "#091011", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Contrast Green Dark", group: "High contrast", light: false, cvd: "", bg: "#0b0c0b", surface: "#121413", surfaceAlt: "#191c1a", border: "#363b37", borderStrong: "#505853", text: "#f6f6f6", text2: "#c4c9c6", muted: "#99a39c", disabled: "#748178", accent: "#66db7e", onAccent: "#09110a", positive: "#61d195", warning: "#e2ad50", danger: "#de6254" },
        { name: "Contrast Warm Light", group: "High contrast", light: true, cvd: "", bg: "#fbfbfb", surface: "#ffffff", surfaceAlt: "#f4f4f3", border: "#dcdad8", borderStrong: "#bbb8b4", text: "#1c1b1a", text2: "#3c3937", muted: "#5a5551", disabled: "#78736d", accent: "#ae5529", onAccent: "#fbfaf9", positive: "#2c965d", warning: "#a7741b", danger: "#a32c1f" },
        { name: "Protanopia Dark (colourblind)", group: "Colourblind", light: false, cvd: "protanopia", bg: "#0f1112", surface: "#17191c", surfaceAlt: "#1e2227", border: "#32383e", borderStrong: "#4a545e", text: "#f1f2f3", text2: "#c1c7cd", muted: "#929eaa", disabled: "#6c7a89", accent: "#aac4df", onAccent: "#090c11", positive: "#e1e119", warning: "#913ae9", danger: "#ca492f" },
        { name: "Protanopia Light (colourblind)", group: "Colourblind", light: true, cvd: "protanopia", bg: "#f5f6f7", surface: "#ffffff", surfaceAlt: "#eceef0", border: "#d6dade", borderStrong: "#afb8c0", text: "#1c1f22", text2: "#444c55", muted: "#63707e", disabled: "#84919f", accent: "#173163", onAccent: "#f9fafb", positive: "#2ba512", warning: "#2323e7", danger: "#5c180a" },
        { name: "Deuteranopia Dark (colourblind)", group: "Colourblind", light: false, cvd: "deuteranopia", bg: "#0f1012", surface: "#17191c", surfaceAlt: "#1e2127", border: "#32363e", borderStrong: "#4a515e", text: "#f1f2f3", text2: "#c1c5cd", muted: "#929aaa", disabled: "#6c7689", accent: "#eadec8", onAccent: "#110e09", positive: "#674ceb", warning: "#e1e119", danger: "#3f84a6" },
        { name: "Deuteranopia Light (colourblind)", group: "Colourblind", light: true, cvd: "deuteranopia", bg: "#f5f6f7", surface: "#ffffff", surfaceAlt: "#eceef0", border: "#d6d9de", borderStrong: "#afb5c0", text: "#1c1e22", text2: "#444a55", muted: "#636c7e", disabled: "#8891a2", accent: "#76522d", onAccent: "#fbfaf9", positive: "#2323e7", warning: "#c35ad8", danger: "#0a0a5c" },
        { name: "Tritanopia Dark (colourblind)", group: "Colourblind", light: false, cvd: "tritanopia", bg: "#120f0f", surface: "#1c1718", surfaceAlt: "#271e20", border: "#3e3234", borderStrong: "#5e4a4d", text: "#f3f1f1", text2: "#cdc1c3", muted: "#aa9296", disabled: "#896c71", accent: "#eac8cd", onAccent: "#11090a", positive: "#e11919", warning: "#27e7e7", danger: "#a6503f" },
        { name: "Tritanopia Light (colourblind)", group: "Colourblind", light: true, cvd: "tritanopia", bg: "#f7f5f5", surface: "#ffffff", surfaceAlt: "#f0eced", border: "#ded6d7", borderStrong: "#c0afb2", text: "#221c1d", text2: "#554447", muted: "#7e6367", disabled: "#a2888d", accent: "#4a1c24", onAccent: "#fbf9f9", positive: "#149cb8", warning: "#e72323", danger: "#d85ac3" },
        { name: "Achromatopsia Dark (colourblind)", group: "Colourblind", light: false, cvd: "achromatopsia", bg: "#0b0b0b", surface: "#131313", surfaceAlt: "#1b1b1b", border: "#383838", borderStrong: "#545454", text: "#f6f6f6", text2: "#cbcbcb", muted: "#a2a2a2", disabled: "#7e7e7e", accent: "#696969", onAccent: "#fbf9f9", positive: "#fafafa", warning: "#adadad", danger: "#d4d4d4" },
        { name: "Achromatopsia Light (colourblind)", group: "Colourblind", light: true, cvd: "achromatopsia", bg: "#fbfbfb", surface: "#ffffff", surfaceAlt: "#f4f4f4", border: "#dadada", borderStrong: "#b8b8b8", text: "#1b1b1b", text2: "#393939", muted: "#555555", disabled: "#777777", accent: "#0a0a0a", onAccent: "#fbf9f9", positive: "#949494", warning: "#4f4f4f", danger: "#292929" }
    ]

    readonly property int themeCount: themes.length
    property int currentIndex: 0

    readonly property var current: themes[Math.max(0, Math.min(currentIndex, themes.length - 1))]
    readonly property string name: current.name
    readonly property string group: current.group

    function themeNameAt(i)   { return themes[Math.max(0, Math.min(i, themes.length - 1))].name }
    function themeGroupAt(i)  { return themes[Math.max(0, Math.min(i, themes.length - 1))].group }
    function groupAt(i)       { return themeGroupAt(i) }
    // "" for an ordinary theme, otherwise the colour vision deficiency it is
    // designed for: protanopia, deuteranopia, tritanopia or achromatopsia.
    function themeCvdAt(i)    { return themes[Math.max(0, Math.min(i, themes.length - 1))].cvd || "" }
    function cvdLabel(kind) {
        if (kind === "protanopia")    return "Protanopia · red-blind"
        if (kind === "deuteranopia")  return "Deuteranopia · green-blind"
        if (kind === "tritanopia")    return "Tritanopia · blue-blind"
        if (kind === "achromatopsia") return "Achromatopsia · no colour vision"
        return ""
    }
    function themeAccentAt(i) { return themes[Math.max(0, Math.min(i, themes.length - 1))].accent }
    function themeSurfaceAt(i){ return themes[Math.max(0, Math.min(i, themes.length - 1))].surface }
    function themeTextAt(i)   { return themes[Math.max(0, Math.min(i, themes.length - 1))].text }
    function themeIsLightAt(i){ return themes[Math.max(0, Math.min(i, themes.length - 1))].light }

    // Group names in catalogue order, for a picker that shows sections rather
    // than one flat list of a hundred names.
    readonly property var groups: {
        var seen = {}, out = []
        for (var i = 0; i < themes.length; ++i) {
            var g = themes[i].group
            if (!seen[g]) { seen[g] = true; out.push(g) }
        }
        return out
    }

    function indicesInGroup(groupName) {
        var out = []
        for (var i = 0; i < themes.length; ++i)
            if (themes[i].group === groupName) out.push(i)
        return out
    }

    // --------------------------------------------------------------- tokens
    readonly property color background:    current.bg
    readonly property color surface:       current.surface
    readonly property color surfaceAlt:    current.surfaceAlt
    readonly property color border:        current.border
    readonly property color borderStrong:  current.borderStrong

    readonly property color text:          current.text
    readonly property color textSecondary: current.text2
    readonly property color textMuted:     current.muted
    // Disabled text stays above the WCAG 3:1 floor against `surface`, so a
    // greyed-out control is still readable. Unreadable disabled labels were a
    // specific complaint about the previous look.
    readonly property color textDisabled:  current.disabled

    readonly property color accent:        current.accent
    readonly property color onAccent:      current.onAccent
    readonly property color positive:      current.positive
    readonly property color warning:       current.warning
    readonly property color danger:        current.danger

    readonly property bool isLight: current.light
    // Empty unless this theme is designed for a colour vision deficiency.
    readonly property string cvd: current.cvd || ""
    readonly property string cvdLabelCurrent: cvdLabel(cvd)

    // ----------------------------------------------------------- typography
    readonly property string fontFamily: "Segoe UI"
    readonly property int fontSizeSmall: 11
    readonly property int fontSizeBody: 13
    readonly property int fontSizeTitle: 16
    readonly property int fontSizeDisplay: 34

    // -------------------------------------------------------------- spacing
    // Alpha for a translucent overlay pill sitting over the plot. Four sites
    // used to hard-code 0.69, which is a number nobody can match by eye.
    readonly property real overlayAlpha: 0.69
    readonly property int gapTight: 4
    readonly property int gap: 8
    readonly property int gapWide: 16
    readonly property int radius: 8
}
