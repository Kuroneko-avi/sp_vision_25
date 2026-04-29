# Dashboard Vendor Assets

These files are checked in so the Dashboard can run from the static HTTP server without external CDN access or an npm build step.

| Library | Version | Local file | Source |
| :--- | :--- | :--- | :--- |
| MQTT.js | 5.15.1 | `mqtt/mqtt.min.js` | `https://cdn.jsdelivr.net/npm/mqtt@5.15.1/dist/mqtt.min.js` |
| ECharts | 6.0.0 | `echarts/echarts.min.js` | `https://cdn.jsdelivr.net/npm/echarts@6.0.0/dist/echarts.min.js` |
| GridStack | 12.6.0 | `gridstack/gridstack-all.js` | `https://cdn.jsdelivr.net/npm/gridstack@12.6.0/dist/gridstack-all.js` |
| GridStack CSS | 12.6.0 | `gridstack/gridstack.min.css` | `https://cdn.jsdelivr.net/npm/gridstack@12.6.0/dist/gridstack.min.css` |

Do not add npm, Vite, React, or Vue for the current Dashboard runtime. If a vendor file must be updated, replace the file and update this table in the same change.
