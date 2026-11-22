// Ignition — phone-side script: settings page (Clay), sun times for the Day
// complication and the hemisphere for the Moon complication (from the phone's
// location), and Moon/Mars distances (from the date alone). All computed
// locally; no web service.
var Clay = require('./clay');
var orbits = require('./orbits');
var clayConfig = require('./config');
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

// NOAA solar position approximation. Returns minutes after local midnight for
// sunrise, solar noon and sunset; polar day/night returns only noon.
function sunTimes(lat, lon, date) {
  var rad = Math.PI / 180;
  var start = Date.UTC(date.getFullYear(), 0, 0);
  var today = Date.UTC(date.getFullYear(), date.getMonth(), date.getDate());
  var dayOfYear = Math.round((today - start) / 86400000);
  var g = 2 * Math.PI / 365 * (dayOfYear - 1 + 0.5);   // fractional year, at noon

  var eqTime = 229.18 * (0.000075 + 0.001868 * Math.cos(g) - 0.032077 * Math.sin(g) -
                         0.014615 * Math.cos(2 * g) - 0.040849 * Math.sin(2 * g));
  var decl = 0.006918 - 0.399912 * Math.cos(g) + 0.070257 * Math.sin(g) -
             0.006758 * Math.cos(2 * g) + 0.000907 * Math.sin(2 * g) -
             0.002697 * Math.cos(3 * g) + 0.00148 * Math.sin(3 * g);

  // Minutes east of UTC at noon that day, so a refresh just after midnight on a
  // clock-change day still uses the right offset.
  var tz = -new Date(date.getFullYear(), date.getMonth(), date.getDate(), 12).getTimezoneOffset();
  var noon = 720 - 4 * lon - eqTime + tz;
  var cosH = (Math.cos(90.833 * rad) - Math.sin(lat * rad) * Math.sin(decl)) /
             (Math.cos(lat * rad) * Math.cos(decl));
  function wrap(m) { return ((Math.round(m) % 1440) + 1440) % 1440; }
  if (cosH > 1 || cosH < -1) return { noon: wrap(noon) };
  var ha = Math.acos(cosH) / rad;                       // degrees
  return { rise: wrap(noon - 4 * ha), noon: wrap(noon), set: wrap(noon + 4 * ha) };
}

// One message at a time to the watch; a failed send is retried once.
var queue = [], sending = false;
function send(msg) {
  queue.push({ msg: msg, tries: 0 });
  if (!sending) sendNext();
}
function sendNext() {
  if (!queue.length) { sending = false; return; }
  sending = true;
  var item = queue[0];
  Pebble.sendAppMessage(item.msg, function () {
    queue.shift();
    sendNext();
  }, function (e) {
    if (++item.tries >= 2) {
      console.log('Send failed: ' + JSON.stringify(e));
      queue.shift();
    }
    setTimeout(sendNext, 1000);
  });
}

function sendSun() {
  navigator.geolocation.getCurrentPosition(function (pos) {
    var t = sunTimes(pos.coords.latitude, pos.coords.longitude, new Date());
    var msg = { SolarNoon: t.noon, SouthHemi: pos.coords.latitude < 0 ? 1 : 0 };
    if (t.rise !== undefined) { msg.Sunrise = t.rise; msg.Sunset = t.set; }
    send(msg);
  }, function (err) {
    console.log('Location error: ' + err.code);
  }, { timeout: 15000, maximumAge: 6 * 60 * 60 * 1000 });
}

// Once the engines started showing the hour, the rim's default moved from
// Hours to Month. Update settings saved by older versions once, so the
// settings page shows the same default the watch now uses.
function migrateSettings() {
  try {
    if (localStorage.getItem('ignition-rev') === '3') return;
    var saved = JSON.parse(localStorage.getItem('clay-settings'));
    // Only settings saved before the engines showed the hour (no EngineOrder yet).
    if (saved && saved.EngineOrder === undefined &&
        (saved.RimMode === undefined || String(saved.RimMode) === '0')) {
      saved.RimMode = '3';
      localStorage.setItem('clay-settings', JSON.stringify(saved));
    }
    localStorage.setItem('ignition-rev', '3');
  } catch (e) {
    console.log('Settings migration skipped: ' + e);
  }
}

// need: bit 1 = sun times (needs location), bit 2 = Moon/Mars distances.
function sendSky(need) {
  if (need & 2) send(orbits.orbitMessage(new Date()));
  if (need & 1) sendSun();
}

Pebble.addEventListener('ready', function () {
  migrateSettings();
  sendSky(3);
});

Pebble.addEventListener('appmessage', function (e) {
  if (e.payload && e.payload.RequestSun !== undefined) sendSky(e.payload.RequestSun || 3);
});

Pebble.addEventListener('showConfiguration', function () {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) return;
  send(clay.getSettings(e.response));
});
