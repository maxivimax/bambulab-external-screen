import fs from "node:fs";
import path from "node:path";
import mqtt from "mqtt";

const configPath = path.resolve("include/app_config.h");
const outputPath = path.resolve("artifacts/live_state.txt");

function readConfig() {
  const text = fs.readFileSync(configPath, "utf8");
  const pick = (name) => {
    const re = new RegExp(`${name}\\[\\]\\s*=\\s*"([^"]+)"`);
    const match = text.match(re);
    return match ? match[1] : "";
  };

  const pickPort = () => {
    const match = text.match(/BAMBU_MQTT_PORT\s*=\s*(\d+)/);
    return match ? Number(match[1]) : 8883;
  };

  return {
    host: pick("BAMBU_PRINTER_IP"),
    port: pickPort(),
    serial: pick("BAMBU_PRINTER_SERIAL"),
    username: pick("BAMBU_MQTT_USERNAME") || "bblp",
    password: pick("BAMBU_ACCESS_CODE"),
  };
}

function writeStateFile(state) {
  fs.mkdirSync(path.dirname(outputPath), { recursive: true });
  const lines = [
    `has_data=${state.hasData ? 1 : 0}`,
    `printing=${state.printing ? 1 : 0}`,
    `stale=0`,
    `percent=${state.percent ?? 0}`,
    `remaining_minutes=${state.remainingMinutes ?? 0}`,
    `current_layer=${state.currentLayer ?? 0}`,
    `total_layers=${state.totalLayers ?? 0}`,
    `nozzle_temp=${state.nozzleTemp ?? 0}`,
    `nozzle_target_temp=${state.nozzleTargetTemp ?? 0}`,
    `bed_temp=${state.bedTemp ?? 0}`,
    `bed_target_temp=${state.bedTargetTemp ?? 0}`,
    `speed_level=${state.speedLevel ?? 0}`,
    `stage_current=${state.stageCurrent ?? -1}`,
    `stage_text=${state.stageText ?? ""}`,
    `job_name=${state.jobName ?? "-"}`,
    `wifi_signal=${state.wifiSignal ?? "-"}`,
    `print_type=${state.printType ?? "-"}`,
    `last_command=${state.lastCommand ?? "-"}`,
  ];
  fs.writeFileSync(outputPath, `${lines.join("\n")}\n`, "utf8");
}

function mergePrintState(prev, print) {
  const gcodeState = print.gcode_state ?? prev.stageText ?? "";
  return {
    hasData: true,
    printing: ["RUNNING", "PREPARE", "SLICING", "PAUSE"].includes(gcodeState),
    percent: print.mc_percent ?? prev.percent ?? 0,
    remainingMinutes: print.mc_remaining_time ?? prev.remainingMinutes ?? 0,
    currentLayer: print.layer_num ?? prev.currentLayer ?? 0,
    totalLayers: print.total_layer_num ?? prev.totalLayers ?? 0,
    nozzleTemp: print.nozzle_temper ?? prev.nozzleTemp ?? 0,
    nozzleTargetTemp: print.nozzle_target_temper ?? prev.nozzleTargetTemp ?? 0,
    bedTemp: print.bed_temper ?? prev.bedTemp ?? 0,
    bedTargetTemp: print.bed_target_temper ?? prev.bedTargetTemp ?? 0,
    speedLevel: print.spd_lvl ?? prev.speedLevel ?? 0,
    stageCurrent: print.stg_cur ?? prev.stageCurrent ?? -1,
    stageText: gcodeState || "UNKNOWN",
    jobName: print.subtask_name ?? prev.jobName ?? "-",
    wifiSignal: print.wifi_signal ?? prev.wifiSignal ?? "-",
    printType: print.print_type ?? prev.printType ?? "-",
    lastCommand: print.command ?? prev.lastCommand ?? "push_status",
  };
}

const cfg = readConfig();
if (!cfg.host || !cfg.serial || !cfg.password) {
  console.error("Missing printer config in include/app_config.h");
  process.exit(1);
}

const requestTopic = `device/${cfg.serial}/request`;
const reportTopic = `device/${cfg.serial}/report`;

let state = {
  hasData: false,
  printing: false,
  percent: 0,
  remainingMinutes: 0,
  currentLayer: 0,
  totalLayers: 0,
  nozzleTemp: 0,
  nozzleTargetTemp: 0,
  bedTemp: 0,
  bedTargetTemp: 0,
  speedLevel: 0,
  stageCurrent: -1,
  stageText: "Waiting for data",
  jobName: "-",
  wifiSignal: "-",
  printType: "-",
  lastCommand: "-",
};

const client = mqtt.connect({
  host: cfg.host,
  port: cfg.port,
  protocol: "mqtts",
  username: cfg.username,
  password: cfg.password,
  rejectUnauthorized: false,
  reconnectPeriod: 3000,
  connectTimeout: 10000,
  clientId: `desktop-preview-${Math.random().toString(16).slice(2, 10)}`,
});

client.on("connect", () => {
  console.log(`Connected to ${cfg.host}:${cfg.port}`);
  client.subscribe(reportTopic, (err) => {
    if (err) {
      console.error("Subscribe failed:", err.message);
      return;
    }

    console.log(`Subscribed to ${reportTopic}`);
    client.publish(
      requestTopic,
      JSON.stringify({
        pushing: {
          sequence_id: "0",
          command: "pushall",
          version: 1,
          push_target: 1,
        },
      })
    );
  });
});

client.on("message", (_topic, payload) => {
  try {
    const root = JSON.parse(payload.toString("utf8"));
    if (!root.print) {
      return;
    }

    state = mergePrintState(state, root.print);
    writeStateFile(state);
  } catch (error) {
    console.error("Bad payload:", error.message);
  }
});

client.on("error", (error) => {
  console.error("MQTT error:", error.message);
});

setInterval(() => {
  if (!client.connected) {
    return;
  }
  client.publish(
    requestTopic,
    JSON.stringify({
      pushing: {
        sequence_id: "0",
        command: "pushall",
        version: 1,
        push_target: 1,
      },
    })
  );
}, 30000);
