#pragma once
// -----------------------------------------------------------------------------
//  WifiQr - the string a phone camera turns into "join this network?".
//
//  One de-facto format, understood by iOS and Android cameras without an app:
//
//      WIFI:T:WPA;S:<ssid>;P:<key>;;
//
//  Parts are separated by semicolons and introduced by colons, so an SSID or
//  key containing either has to say so: `\`, `;`, `,`, `:` and `"` are all
//  backslash-escaped. Nothing this device generates contains one today, the
//  name being fixed and the key drawn from letters and digits. It is done
//  anyway, because the alternative is a rule that holds until somebody changes
//  a constant, and the failure is a QR that silently encodes another network.
//
//  Hardware-free; unit-tested on the host.
// -----------------------------------------------------------------------------
#include <cstddef>

namespace sd {

/// Longest payload worth building. Well past what an SSID and a WPA2 key need
/// even fully escaped, and it keeps the QR inside version 3.
constexpr size_t kWifiQrMax = 96;

/// Builds the payload into `out`, NUL-terminated, and returns its length.
///
/// A `pass` shorter than the eight characters WPA2 requires is taken as an open
/// network and encoded `T:nopass` with no key, matching what RadioService does
/// with the same string, so the code and the network cannot disagree.
///
/// Returns 0 and writes an empty string when it would not fit, rather than
/// encoding a truncated network nobody can join.
size_t wifiQrPayload(const char* ssid, const char* pass, char* out, size_t cap);

}  // namespace sd
