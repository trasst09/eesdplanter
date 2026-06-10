const SHEET_NAME = "SmartGardenData";

const HEADERS = [
  "Timestamp",
  "Moisture",
  "Moisture Raw",
  "Threshold",
  "Air Temp C",
  "Humidity",
  "Soil Temp C",
  "Light Lux",
  "Battery V",
  "Water Pump",
  "Filter Pump",
  "Auto Mode",
  "Raw Line"
];

function doGet(e) {
  try {
    const params = e && e.parameter ? e.parameter : {};
    const action = params.action || "";

    // If user opens the plain /exec URL, show dashboard website.
    if (!action && Object.keys(params).length === 0) {
      return HtmlService
        .createHtmlOutputFromFile("index")
        .setTitle("Smart Garden Dashboard")
        .setXFrameOptionsMode(HtmlService.XFrameOptionsMode.ALLOWALL);
    }

    // API endpoint: test if Apps Script is working.
    if (action === "test") {
      return jsonResponse({
        status: "ok",
        message: "Google Apps Script is working"
      });
    }

    // API endpoint: latest row.
    if (action === "latest") {
      return jsonResponse(getLatestData());
    }

    // API endpoint: recent rows.
    if (action === "recent") {
      return jsonResponse(getRecentData());
    }

    // Otherwise treat request as ESP8266 data upload.
    return jsonResponse(saveData(params));

  } catch (err) {
    return jsonResponse({
      status: "error",
      message: String(err),
      stack: err && err.stack ? err.stack : ""
    });
  }
}

function saveData(params) {
  const sheet = getSheet();

  const row = [
    new Date(),
    clean(params.moisture),
    clean(params.moistureRaw),
    clean(params.threshold),
    clean(params.airTemp),
    clean(params.humidity),
    clean(params.soilTemp),
    clean(params.light),
    clean(params.battery),
    clean(params.waterPump),
    clean(params.filterPump),
    clean(params.autoMode),
    clean(params.raw)
  ];

  sheet.appendRow(row);

  return {
    status: "ok",
    message: "Data saved",
    data: rowToObject(row)
  };
}

function getLatestData() {
  const sheet = getSheet();
  const lastRow = sheet.getLastRow();

  if (lastRow < 2) {
    return {
      status: "empty",
      message: "No data yet"
    };
  }

  const row = sheet.getRange(lastRow, 1, 1, HEADERS.length).getValues()[0];

  return {
    status: "ok",
    data: rowToObject(row)
  };
}

function getRecentData() {
  const sheet = getSheet();
  const lastRow = sheet.getLastRow();

  if (lastRow < 2) {
    return {
      status: "empty",
      data: []
    };
  }

  const count = Math.min(20, lastRow - 1);
  const startRow = lastRow - count + 1;

  const rows = sheet
    .getRange(startRow, 1, count, HEADERS.length)
    .getValues();

  return {
    status: "ok",
    data: rows.map(rowToObject)
  };
}

function getSheet() {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  let sheet = ss.getSheetByName(SHEET_NAME);

  if (!sheet) {
    sheet = ss.insertSheet(SHEET_NAME);
  }

  ensureHeaders(sheet);

  return sheet;
}

function ensureHeaders(sheet) {
  if (sheet.getLastRow() === 0) {
    sheet.appendRow(HEADERS);
    return;
  }

  const firstRow = sheet
    .getRange(1, 1, 1, HEADERS.length)
    .getValues()[0];

  let headersMatch = true;

  for (let i = 0; i < HEADERS.length; i++) {
    if (firstRow[i] !== HEADERS[i]) {
      headersMatch = false;
      break;
    }
  }

  if (!headersMatch) {
    sheet.insertRowBefore(1);
    sheet.getRange(1, 1, 1, HEADERS.length).setValues([HEADERS]);
  }
}

function rowToObject(row) {
  return {
    timestamp: formatTimestamp(row[0]),
    moisture: row[1],
    moistureRaw: row[2],
    threshold: row[3],
    airTemp: row[4],
    humidity: row[5],
    soilTemp: row[6],
    light: row[7],
    battery: row[8],
    waterPump: row[9],
    filterPump: row[10],
    autoMode: row[11],
    raw: row[12]
  };
}

function formatTimestamp(value) {
  if (!value) {
    return "";
  }

  if (Object.prototype.toString.call(value) === "[object Date]") {
    return Utilities.formatDate(
      value,
      Session.getScriptTimeZone(),
      "yyyy-MM-dd HH:mm:ss"
    );
  }

  return String(value);
}

function clean(value) {
  if (value === undefined || value === null) {
    return "";
  }

  return String(value);
}

function jsonResponse(obj) {
  return ContentService
    .createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}
