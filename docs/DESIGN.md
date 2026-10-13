# Signal explorer design

Reading this as an audio-analysis demonstration for audio engineers and developers reviewing a portfolio, with the visual language of a restrained measurement tool. This is an evolution of the existing viewer, preserving the data, interaction order, OtoFrame wordmark, waveform mark, and orange accent family.

Reference: [Taste skill, design-taste-frontend](https://github.com/Leonxlnx/taste-skill/blob/d237dd399d57524945c0c81327da5da19f7a468d/skills/taste-skill/SKILL.md). The skill primarily covers landing pages and portfolios and explicitly excludes dense analysis interfaces. Its typography, spacing, accent, and redesign principles are used here; marketing-only requirements such as generated photography and scroll choreography do not apply to the actual data plots. The viewer retains its native HTML/CSS foundation.

## Audit and direction

The original viewer used a dark navy surface, rounded boxes around each metric, orange timeline data, mint chroma/spectrum data, and a system font. It had working keyboard controls, file reading, inline errors, and a responsive layout. The repeated metric boxes and multiple accent colors gave competing prominence to supporting values and primary graphs.

The direction is:

- **DESIGN_VARIANCE: 6.** A wider primary timeline with a narrower chroma plot. Small screens collapse to a single column.
- **MOTION_INTENSITY: 3.** Feedback for button presses and chroma updates only. No automatic motion or decorative loops. Reduced-motion settings disable bar transitions.
- **VISUAL_DENSITY: 6.** Compact instrument readings, consistent units, more space around plots, and explanatory detail in a native disclosure.

Geist is used for interface copy, Geist Mono for numerical values and chart annotations. Both fonts are embedded in the standalone HTML. The C++ library remains independent of the viewer's font assets.

Metric readings use alignment and sparse dividers rather than separate cards. Orange is the sole data accent; light and dark themes use the same hierarchy through semantic CSS variables. The System option follows the browser's color-scheme preference. Button labels and form IDs are preserved. No user recording, artificial endorsement, or invented performance metric is used as decoration.

## Applicable review checks

Check both themes at desktop and mobile sizes, keyboard slider operation, descriptor selection, report loading/rejection, readable text/focus states, loading feedback, reduced-motion fallback, and embedded font/license validity. Capture actual browser screenshots for the README.

Numerical result generation and the audio-report schema are unchanged. This is a viewer update. No full Lighthouse or physical-device performance audit is claimed.
