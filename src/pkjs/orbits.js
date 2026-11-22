// Distances from Earth to the Moon and Mars, computed on the phone.
//
// Moon: the largest terms of the lunar distance series from Meeus,
//   "Astronomical Algorithms" (ch. 47) — good to a few hundred km.
// Mars: Keplerian orbits for Earth and Mars from JPL's "Approximate Positions
//   of the Planets" (table 1, valid 1800-2050) — good to about 0.01 AU.

var RAD = Math.PI / 180;

function julianCenturies(date) {
  var jd = date.getTime() / 86400000 + 2440587.5;
  return (jd - 2451545.0) / 36525;
}

function moonDistanceKm(date) {
  var T = julianCenturies(date);
  var D  = (297.8501921 + 445267.1114034 * T) * RAD;   // mean elongation
  var M  = (357.5291092 + 35999.0502909 * T) * RAD;    // sun's mean anomaly
  var Mp = (134.9633964 + 477198.8675055 * T) * RAD;   // moon's mean anomaly
  var F  = (93.2720950 + 483202.0175233 * T) * RAD;    // argument of latitude
  var terms = [
    // [D, M, M', F, coefficient (km)]
    [0, 0, 1, 0, -20905.355],
    [2, 0, -1, 0, -3699.111],
    [2, 0, 0, 0, -2955.968],
    [0, 0, 2, 0, -569.925],
    [0, 1, 0, 0, 48.888],
    [0, 0, 0, 2, -3.149],
    [2, 0, -2, 0, 246.158],
    [2, -1, -1, 0, -152.138],
    [2, 0, 1, 0, -170.733],
    [2, -1, 0, 0, -204.586],
    [0, 1, -1, 0, -129.620],
    [1, 0, 0, 0, 108.743],
    [0, 1, 1, 0, 104.755],
    [2, 0, 0, -2, 10.321],
    [0, 0, 1, -2, 79.661],
    [4, 0, -1, 0, -34.782],
    [0, 0, 3, 0, -23.210],
    [4, 0, -2, 0, -21.636],
    [2, 1, -1, 0, 24.208],
    [2, 1, 0, 0, 30.824]
  ];
  var d = 385000.56;
  for (var i = 0; i < terms.length; i++) {
    var t = terms[i];
    d += t[4] * Math.cos(t[0] * D + t[1] * M + t[2] * Mp + t[3] * F);
  }
  return d;
}

// Heliocentric ecliptic position (AU) from JPL mean elements.
function planetPosition(el, T) {
  var a = el.a[0] + el.a[1] * T;
  var e = el.e[0] + el.e[1] * T;
  var I = (el.I[0] + el.I[1] * T) * RAD;
  var L = el.L[0] + el.L[1] * T;
  var varpi = el.w[0] + el.w[1] * T;
  var Om = (el.O[0] + el.O[1] * T) * RAD;
  var w = varpi * RAD - Om;                               // argument of perihelion
  var M = ((L - varpi) % 360) * RAD;                      // mean anomaly
  var E = M;
  for (var k = 0; k < 8; k++) E = E - (E - e * Math.sin(E) - M) / (1 - e * Math.cos(E));
  var xp = a * (Math.cos(E) - e);
  var yp = a * Math.sqrt(1 - e * e) * Math.sin(E);
  var cw = Math.cos(w), sw = Math.sin(w), cO = Math.cos(Om), sO = Math.sin(Om);
  var cI = Math.cos(I), sI = Math.sin(I);
  return {
    x: (cw * cO - sw * sO * cI) * xp + (-sw * cO - cw * sO * cI) * yp,
    y: (cw * sO + sw * cO * cI) * xp + (-sw * sO + cw * cO * cI) * yp,
    z: (sw * sI) * xp + (cw * sI) * yp
  };
}

var EARTH = {   // Earth-Moon barycenter
  a: [1.00000261, 0.00000562], e: [0.01671123, -0.00004392],
  I: [-0.00001531, -0.01294668], L: [100.46457166, 35999.37244981],
  w: [102.93768193, 0.32327364], O: [0.0, 0.0]
};
var MARS = {
  a: [1.52371034, 0.00001847], e: [0.09339410, 0.00007882],
  I: [1.84969142, -0.00813131], L: [-4.55343205, 19140.30268499],
  w: [-23.94362959, 0.44441088], O: [49.55953891, -0.29257343]
};

function marsDistanceAU(date) {
  var T = julianCenturies(date);
  var e = planetPosition(EARTH, T), m = planetPosition(MARS, T);
  var dx = m.x - e.x, dy = m.y - e.y, dz = m.z - e.z;
  return Math.sqrt(dx * dx + dy * dy + dz * dz);
}

// The next moment Mars is closest to Earth (a local minimum of the distance),
// searched day by day for up to ~2.5 years, then refined to the hour.
function nextMarsClosest(date) {
  var DAY = 86400000, HOUR = 3600000;
  var t0 = date.getTime() - DAY;
  var prev = marsDistanceAU(new Date(t0)), cur = marsDistanceAU(new Date(t0 + DAY));
  for (var i = 1; i < 900; i++) {
    var next = marsDistanceAU(new Date(t0 + (i + 1) * DAY));
    if (cur < prev && cur <= next) {
      var best = t0 + i * DAY, bestD = cur;
      for (var h = -24; h <= 24; h++) {
        var t = t0 + i * DAY + h * HOUR, d = marsDistanceAU(new Date(t));
        if (d < bestD) { bestD = d; best = t; }
      }
      return new Date(best);
    }
    prev = cur; cur = next;
  }
  return null;
}

// Local calendar day (days since 1970-01-01) of a moment, in the phone's timezone.
function localDay(d) {
  return Math.round(Date.UTC(d.getFullYear(), d.getMonth(), d.getDate()) / 86400000);
}

// What the watch needs: each distance, and whether it is shrinking.
function orbitMessage(date) {
  var hour = new Date(date.getTime() + 3600000);
  var day = new Date(date.getTime() + 86400000);
  var moon = moonDistanceKm(date), mars = marsDistanceAU(date);
  return {
    MoonDistKm: Math.round(moon),
    MoonApproach: moonDistanceKm(hour) < moon ? 1 : 0,
    MarsDistMilliAU: Math.round(mars * 1000),
    MarsApproach: marsDistanceAU(day) < mars ? 1 : 0,
    MarsClosestDay: (function () { var c = nextMarsClosest(date); return c ? localDay(c) : 0; })()
  };
}

module.exports = { moonDistanceKm: moonDistanceKm, marsDistanceAU: marsDistanceAU,
                   nextMarsClosest: nextMarsClosest, orbitMessage: orbitMessage };
