# Razer Kraken V4 battery protocol

Device tested: wireless receiver `1532:056c`, HID interface 5, vendor usage page
`0xff14`, usage 1. Wired PID `056b` and other Razer models are not supported by
this implementation. The device also exposes `0xff13` and standard LampArray
collections; battery queries use the `0xff14` Output report.

Traffic was captured with Linux usbmon while Synapse 4 ran in a Windows 11 VM
with USB passthrough. Synapse release notes reported version
`4.0.86.2609150817`. Battery readings at 44% and 46% matched the captured payload,
and replaying the battery query from Linux returned 49% as charging progressed.
The C++ implementation subsequently returned 55% / BATTERY_CHARGING with external
power, then 83% / BATTERY_AVAILABLE after disconnecting the external charger.
With the headset switched off and receiver attached, the query timed out and
HeadsetControl reported BATTERY_UNAVAILABLE without displaying a cached percentage.

## Requests

Both requests are exactly 64 bytes including report ID 2. Synapse uses
`SET_REPORT` with `bmRequestType=0x21`, `bRequest=0x09`, `wValue=0x0202`,
`wIndex=5`, `wLength=64`: an Output report, not a Feature report.

| Offset | Battery query | Charging query |
| --- | --- | --- |
| 0–10 | `02 00 60 00 00 00 04 00 00 80 21` | `02 00 60 00 00 00 04 00 00 80 2a` |
| 11–61 | All zero | All zero |
| 62 | `c7` | `cc` |
| 63 | `00` | `00` |

These requests are copied from captured Synapse packets. No checksum-generation
rule, initialization sequence or Feature request is needed for the implementation.

## Responses

Responses arrive through interrupt IN endpoint `0x84`, also 64 bytes including
the report ID. The first 13 bytes are:

```
02 02 60 00 00 00 05 00 80 80 <command> 01 01
```

Byte 13 is the battery percentage (command `21`) or charging flag (command `2a`,
0 without external power, 1 while charging). Captured battery values include
`2c` (44%), `2d` (45%), `2e` (46%); Linux replay returned `31` (49%).

Asynchronous notifications begin `02 0a 60`, include an incrementing field at
offset 3, and use `02` instead of `01` at offset 11. They must not be accepted
as synchronous replies. Other reports may also arrive on the same HID node.
The implementation sends each query once and reads until a matching reply or
a fixed deadline. It checks the full response length and value range.

## Validation limits

- Retain minimal captures for the PR.
- Linux HIDAPI hidraw has been tested; HIDAPI on macOS/Windows
  has not been tested on hardware.
