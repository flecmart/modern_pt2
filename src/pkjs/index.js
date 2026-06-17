var Clay = require('pebble-clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

// WMO weather code -> simplified condition integer
// 0=clear, 1=partly cloudy, 2=cloudy, 3=rain, 4=snow, 5=thunderstorm
function wmoToCondition(code) {
  if (code === 0)                                return 0; // clear sky
  if (code <= 2)                                 return 1; // mainly/partly clear
  if (code === 3)                                return 2; // overcast
  if (code >= 51 && code <= 67)                  return 3; // drizzle / rain
  if (code >= 71 && code <= 77)                  return 4; // snow
  if (code >= 80 && code <= 82)                  return 3; // showers
  if (code === 85 || code === 86)                return 4; // snow showers
  if (code >= 95 && code <= 99)                  return 5; // thunderstorm
  return 2; // default: cloudy
}

function fetchWeather(latitude, longitude) {
  var url = 'https://api.open-meteo.com/v1/forecast' +
    '?latitude=' + latitude +
    '&longitude=' + longitude +
    '&current_weather=true' +
    '&temperature_unit=celsius';

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
  // Refresh weather every 30 minutes while the phone app is alive
  setInterval(requestWeather, 30 * 60 * 1000);
});

Pebble.addEventListener('appmessage', function(e) {
  console.log('AppMessage received from watch: ' + JSON.stringify(e.payload));
});
