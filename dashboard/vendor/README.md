# Dashboard Vendor Assets

These files are checked in so the Dashboard can run from the static HTTP server without external CDN access or an npm build step.

| Library | Version | License | Local file | Source | License URL |
| :--- | :--- | :--- | :--- | :--- | :--- |
| MQTT.js | 5.15.1 | MIT | `mqtt/mqtt.min.js` | `https://cdn.jsdelivr.net/npm/mqtt@5.15.1/dist/mqtt.min.js` | `https://github.com/mqttjs/MQTT.js/blob/main/LICENSE.md` |
| ECharts | 6.0.0 | Apache-2.0 | `echarts/echarts.min.js` | `https://cdn.jsdelivr.net/npm/echarts@6.0.0/dist/echarts.min.js` | `https://github.com/apache/echarts/blob/master/LICENSE` |
| GridStack | 12.6.0 | MIT | `gridstack/gridstack-all.js` | `https://cdn.jsdelivr.net/npm/gridstack@12.6.0/dist/gridstack-all.js` | `https://github.com/gridstack/gridstack.js/blob/master/LICENSE` |
| GridStack CSS | 12.6.0 | MIT | `gridstack/gridstack.min.css` | `https://cdn.jsdelivr.net/npm/gridstack@12.6.0/dist/gridstack.min.css` | `https://github.com/gridstack/gridstack.js/blob/master/LICENSE` |

Do not add npm, Vite, React, or Vue for the current Dashboard runtime. If a vendor file must be updated, replace the file and update this table in the same change.
