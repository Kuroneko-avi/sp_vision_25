export const DEFAULT_ROBOT_ID = "myrobot";

export const TOPIC_SUFFIX = Object.freeze({
  DATA: "data",
  LOG: "log",
  PARAMS_SCHEMA: "params/schema",
  PARAMS_CURRENT: "params/current",
  CONTROL_PARAM: "control/param",
  CONTROL_CMD: "control/cmd",
  CONTROL_ACK: "control/ack"
});

export const QOS = Object.freeze({
  TELEMETRY: 0,
  LOG: 0,
  PARAMS: 0,
  CONTROL_PUBLISH: 1,
  ACK: 1
});

export function sanitizeRobotId(value) {
  const robotId = String(value || "").trim() || DEFAULT_ROBOT_ID;
  return robotId.replace(/[\/+#+\s]/g, "_");
}

export function buildTopics(robotId) {
  const cleanRobotId = sanitizeRobotId(robotId);
  return {
    data: `${cleanRobotId}/${TOPIC_SUFFIX.DATA}`,
    log: `${cleanRobotId}/${TOPIC_SUFFIX.LOG}`,
    paramsSchema: `${cleanRobotId}/${TOPIC_SUFFIX.PARAMS_SCHEMA}`,
    paramsCurrent: `${cleanRobotId}/${TOPIC_SUFFIX.PARAMS_CURRENT}`,
    controlParam: `${cleanRobotId}/${TOPIC_SUFFIX.CONTROL_PARAM}`,
    controlCmd: `${cleanRobotId}/${TOPIC_SUFFIX.CONTROL_CMD}`,
    controlAck: `${cleanRobotId}/${TOPIC_SUFFIX.CONTROL_ACK}`
  };
}

export function getSubscriptions(topics) {
  return {
    [topics.data]: { qos: QOS.TELEMETRY },
    [topics.log]: { qos: QOS.LOG },
    [topics.paramsSchema]: { qos: QOS.PARAMS },
    [topics.paramsCurrent]: { qos: QOS.PARAMS },
    [topics.controlAck]: { qos: QOS.ACK }
  };
}

export function parseTelemetryPayload(message) {
  if (isPlainObject(message?.values)) {
    return { values: message.values, source: "values" };
  }
  if (isPlainObject(message?.fields)) {
    return { values: message.fields, source: "fields" };
  }
  return { values: null, source: "" };
}

export function validateParamsSchema(message) {
  if (!message || !Array.isArray(message.params)) {
    return [];
  }
  return message.params
    .filter((param) => param && typeof param.key === "string" && typeof param.type === "string")
    .map((param) => ({ ...param }));
}

export function validateParamsCurrent(message) {
  if (!isPlainObject(message?.values)) {
    return null;
  }
  return message.values;
}

export function buildControlParamPayload(key, value) {
  return {
    request_id: makeRequestId("param"),
    key,
    value,
    timestamp: Date.now()
  };
}

export function buildControlCmdPayload(command, args = {}) {
  return {
    request_id: makeRequestId("cmd"),
    command,
    args: isPlainObject(args) ? args : {},
    timestamp: Date.now()
  };
}

export function makeRequestId(prefix) {
  const random = Math.random().toString(16).slice(2, 10);
  return `${prefix}_${Date.now()}_${random}`;
}

export function isPlainObject(value) {
  return Boolean(value) && typeof value === "object" && !Array.isArray(value);
}
