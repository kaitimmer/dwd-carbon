/**
 * Self-check for DWD cloud layer alignment and fallback (run: npm test).
 */

var assert = require('assert');
var dwd = require('../src/pkjs/dwd-weather');

var HOUR_S = 3600;
var T0 = 1700000000 - (1700000000 % HOUR_S);

function brightSky(startS, n, offset) {
	var out = [];
	for (var i = 0; i < n; i++) {
		out.push({
			timestamp: new Date((startS + i * HOUR_S) * 1000).toISOString().replace('Z', offset || '+00:00'),
			temperature: 10, icon: 'cloudy', cloud_cover: 100,
		});
	}
	return out;
}

function openMeteo(startS, n) {
	var h = { time: [], cloud_cover: [], cloud_cover_low: [], cloud_cover_mid: [], cloud_cover_high: [] };
	for (var i = 0; i < n; i++) {
		h.time.push(startS + i * HOUR_S);
		h.cloud_cover.push(50 + i);
		h.cloud_cover_low.push(i);
		h.cloud_cover_mid.push(i + 1);
		h.cloud_cover_high.push(i + 2);
	}
	return h;
}

// Open-Meteo starts one hour later than Bright Sky: align by timestamp, not index.
var bs = brightSky(T0, 3);
var shifted = openMeteo(T0 + HOUR_S, 5);
assert.strictEqual(dwd.alignCloudLayers(bs, shifted), null, 'missing first hour -> fallback');

var aligned = dwd.alignCloudLayers(brightSky(T0 + HOUR_S, 3), shifted);
assert.deepStrictEqual(aligned.cloud_cover, [50, 51, 52]);
assert.deepStrictEqual(aligned.cloud_cover_low, [0, 1, 2]);
assert.deepStrictEqual(aligned.cloud_cover_high, [2, 3, 4]);

// Non-UTC offset in the Bright Sky timestamp still matches the same instant.
var offsetBs = [{ timestamp: new Date((T0 + 4 * HOUR_S) * 1000).toISOString().slice(0, 19) + '+02:00' }];
assert.deepStrictEqual(dwd.alignCloudLayers(offsetBs, openMeteo(T0, 4)).cloud_cover_low, [2]);

// Null values, missing arrays or no data -> null.
var withNull = openMeteo(T0, 3);
withNull.cloud_cover_mid[1] = null;
assert.strictEqual(dwd.alignCloudLayers(brightSky(T0, 3), withNull), null);
assert.strictEqual(dwd.alignCloudLayers(brightSky(T0, 3), null), null);

// buildPayloadFields: Open-Meteo data used when available...
var current = { temperature: 10, icon: 'cloudy' };
var ok = dwd.buildPayloadFields(brightSky(T0, 3), current, 'celsius', 52.5, 13.4, openMeteo(T0, 3));
assert.deepStrictEqual(ok.cloud_cover, [50, 51, 52]);
assert.deepStrictEqual(ok.cloud_cover_mid, [1, 2, 3]);

// ...and icon-based total with empty layers when the call failed.
var fallback = dwd.buildPayloadFields(brightSky(T0, 3), current, 'celsius', 52.5, 13.4, null);
assert.deepStrictEqual(fallback.cloud_cover, [90, 90, 90]);
assert.deepStrictEqual(fallback.cloud_cover_low, []);
assert.deepStrictEqual(fallback.cloud_cover_high, []);

// Fake retryXhr that records requested URLs; the Open-Meteo call can be made to fail.
function fakeRetryXhr(cloudFails, urls) {
	return function (url, attempts, delay, label, cb) {
		urls.push(url);
		if (url.indexOf('open-meteo') >= 0) {
			if (cloudFails) cb('boom');
			else cb(null, JSON.stringify({ hourly: openMeteo(startOfHour(), 36) }));
		} else if (url.indexOf('/current_weather') >= 0) {
			cb(null, JSON.stringify({ weather: current }));
		} else {
			cb(null, JSON.stringify({ weather: brightSky(startOfHour(), 36) }));
		}
	};
}
function startOfHour() {
	return Math.floor(Date.now() / 1000 / HOUR_S) * HOUR_S;
}
function hasOpenMeteoCall(urls) {
	return urls.some(function (u) { return u.indexOf('open-meteo') >= 0; });
}

// Layered on: Open-Meteo is queried and its layers are used.
var urlsOn = [];
dwd.fetchDwdWeather(52.5, 13.4, 'celsius', true, fakeRetryXhr(false, urlsOn), function (err, f) {
	assert.ifError(err);
	assert.strictEqual(f.cloud_cover[0], 50);
	assert.strictEqual(f.cloud_cover_low.length, 36);
});
assert.ok(hasOpenMeteoCall(urlsOn), 'layered on -> Open-Meteo call');

// Layered on but Open-Meteo fails: DWD data still delivered with icon-based total.
dwd.fetchDwdWeather(52.5, 13.4, 'celsius', true, fakeRetryXhr(true, []), function (err, f) {
	assert.ifError(err);
	assert.strictEqual(f.cloud_cover[0], 90);
	assert.deepStrictEqual(f.cloud_cover_low, []);
});

// Layered off: no Open-Meteo call at all, icon-based total.
var urlsOff = [];
dwd.fetchDwdWeather(52.5, 13.4, 'celsius', false, fakeRetryXhr(false, urlsOff), function (err, f) {
	assert.ifError(err);
	assert.strictEqual(f.cloud_cover[0], 90);
	assert.deepStrictEqual(f.cloud_cover_low, []);
});
assert.ok(!hasOpenMeteoCall(urlsOff), 'layered off -> no Open-Meteo call');

console.log('dwd cloud layer checks passed');
