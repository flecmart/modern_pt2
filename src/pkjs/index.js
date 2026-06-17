var Clay = require('pebble-clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

function getUseFahrenheit() {
  try {
    var settings = JSON.parse(localStorage.getItem('clay-settings')) || {};
    return !!JSON.parse(settings.useFahrenheit);
  } catch (e) {
    return false;
  }
}

function wmoToCondition(code) {
  if (code === 0)                                return 0;
  if (code <= 2)                                 return 1;
  if (code === 3)                                return 2;
  if (code >= 51 && code <= 67)                  return 3;
  if (code >= 71 && code <= 77)                  return 4;
  if (code >= 80 && code <= 82)                  return 3;
  if (code === 85 || code === 86)                return 4;
  if (code >= 95 && code <= 99)                  return 5;
  return 2;
}

function fetchWeather(latitude, longitude) {
  var unit = getUseFahrenheit() ? 'fahrenheit' : 'celsius';
  var url = 'https://api.open-meteo.com/v1/forecast' +
    '?latitude=' + latitude +
    '&longitude=' + longitude +
    '&current_weather=true' +
    '&temperature_unit=' + unit;

  var req = new XMLHttpRequest();
  req.open('GET', url, true);
  req.onload = function() {
    if (req.status === 200) {
      try {
        var response = JSON.parse(req.responseText);
        var weather = response.current_weather;
        var temp = Math.round(weather.temperature);
        var condition = wmoToCondition(weather.weathercode);

        Pebble.sendAppMessage(
          { weatherTemperature: temp, weatherCondition: condition },
          function() { console.log('Weather sent to watch'); },
          function(e) { console.log('Weather send failed: ' + JSON.stringify(e)); }
        );
      } catch (err) {
        console.log('Weather parse error: ' + err);
      }
    } else {
      console.log('Weather fetch HTTP error: ' + req.status);
    }
  };
  req.onerror = function() {
    console.log('Weather fetch network error');
  };
  req.send();
}

function requestWeather() {
  navigator.geolocation.getCurrentPosition(
    function(pos) {
      fetchWeather(pos.coords.latitude, pos.coords.longitude);
    },
    function(err) {
      console.log('Geolocation error: ' + err.message);
    },
    { timeout: 15000, maximumAge: 300000 }
  );
}

Pebble.addEventListener('ready', function() {
  console.log('PebbleKit JS ready');
  requestWeather();
  setInterval(requestWeather, 30 * 60 * 1000);
});
