module.exports = [
  {
    "type": "heading",
    "defaultValue": "Modern Watchface"
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Info Slots"
      },
      {
        "type": "select",
        "messageKey": "leftSlot",
        "label": "Left (9 o'clock)",
        "defaultValue": "1",
        "options": [
          { "label": "None",       "value": "0" },
          { "label": "Weather",    "value": "1" },
          { "label": "Date",       "value": "2" },
          { "label": "Heart Rate", "value": "3" },
          { "label": "Steps",      "value": "4" },
          { "label": "Battery",    "value": "5" }
        ]
      },
      {
        "type": "select",
        "messageKey": "rightSlot",
        "label": "Right (3 o'clock)",
        "defaultValue": "3",
        "options": [
          { "label": "None",       "value": "0" },
          { "label": "Weather",    "value": "1" },
          { "label": "Date",       "value": "2" },
          { "label": "Heart Rate", "value": "3" },
          { "label": "Steps",      "value": "4" },
          { "label": "Battery",    "value": "5" }
        ]
      },
      {
        "type": "select",
        "messageKey": "bottomSlot",
        "label": "Bottom (6 o'clock)",
        "defaultValue": "2",
        "options": [
          { "label": "None",       "value": "0" },
          { "label": "Weather",    "value": "1" },
          { "label": "Date",       "value": "2" },
          { "label": "Heart Rate", "value": "3" },
          { "label": "Steps",      "value": "4" },
          { "label": "Battery",    "value": "5" }
        ]
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Appearance"
      },
      {
        "type": "select",
        "messageKey": "handStyle",
        "label": "Hand Style",
        "defaultValue": "2",
        "options": [
          { "label": "Solid",     "value": "0" },
          { "label": "Outline",  "value": "1" },
          { "label": "Diamond",  "value": "2" }
        ]
      },
      {
        "type": "select",
        "messageKey": "accentColor",
        "label": "Info & Logo Color",
        "defaultValue": "255",
        "options": [
          { "label": "White",          "value": "255" },
          { "label": "Cyan",           "value": "207" },
          { "label": "Electric Blue",  "value": "195" },
          { "label": "Mint Green",     "value": "206" },
          { "label": "Green",          "value": "204" },
          { "label": "Yellow",         "value": "252" },
          { "label": "Chrome Yellow",  "value": "249" },
          { "label": "Orange",         "value": "248" },
          { "label": "Red",            "value": "240" },
          { "label": "Magenta",        "value": "243" },
          { "label": "Vivid Violet",   "value": "199" }
        ]
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Weather"
      },
      {
        "type": "toggle",
        "messageKey": "useFahrenheit",
        "label": "Use Fahrenheit (°F)",
        "defaultValue": false
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Behavior"
      },
      {
        "type": "toggle",
        "messageKey": "displaySeconds",
        "label": "Show Seconds Hand",
        "defaultValue": false
      },
      {
        "type": "toggle",
        "messageKey": "hourVibration",
        "label": "Hourly Vibration",
        "defaultValue": false
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save"
  }
];
