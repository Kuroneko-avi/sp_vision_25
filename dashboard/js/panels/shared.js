export function toTimestampMs(value) {
  const timestamp = Number(value);
  if (!Number.isFinite(timestamp)) {
    return Date.now();
  }
  return timestamp > 100000000000 ? timestamp : timestamp * 1000;
}

export function formatTime(timestamp) {
  return new Date(timestamp).toLocaleTimeString();
}

export function formatDateTime(timestamp) {
  return new Date(timestamp).toLocaleString();
}

export function escapeHtml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}

export function formatRawValue(value) {
  if (value === undefined || value === null) {
    return "";
  }
  if (typeof value === "object") {
    try {
      return JSON.stringify(value);
    } catch (error) {
      return String(value);
    }
  }
  return String(value);
}
