# GM65 setup for a new ESP32 kiosk

**Reusable onboarding reference; physical scanner configuration remains unverified.** Evidence checked 2026-11-23 (ICT). This document is the default starting point for a new M910 GM65; it is not proof that any particular unit has these settings.

## Evidence and boundaries

The [GROW GM65 User Manual V1.3 (September 2016)](https://www.sunrom.com/download/767.pdf) identifies TTL-232, 9600 baud, 8 data bits, no parity and one stop bit as its default (printed page 5, PDF page 9). The setup symbols are QR images. Their raw bytes were extracted from the cited PDF using a QR decoder; the leading byte is `0x80` and the last byte is `0x2e`. These are manual-derived values, **not evidence of a scan or a tested configuration**. See the manifest for hexadecimal bytes; do not treat them as plain UTF-8 strings.

On 2026-11-23, M910 test board USB/ROM MAC `84:C7:BB:79:39:84` ran a local diagnostic based on firmware 5.6.6. Its USB console acknowledged `scanner-baud 9600`; a later status reported UART1 GPIO44 at 9600 with **zero received bytes**. The capture then logged scanner test start but no scanner frame. Thus there is **no evidence in the inspected logs of a GM65 setup barcode scanned, scan order, scanner ACK, module baud change, suffix, or successful QR**. The 9600 command changes the ESP receiver, not the scanner's EEPROM. Another GM65-family module previously measured at 115200; do not assume every unit uses factory defaults. Private logs, which can contain credentials or scanned data, remain on M910 at `/home/mesflow/firmware/esp-kiosk/newboard-3984-20261123/`; do not publish them.

## Wiring and candidate procedure

1. Confirm model, supply and pinout for the specific module. M910 profile uses GM65 TXD → ESP32-S3 GPIO44/RX and common ground, with ESP UART TX unused. The module's power and logic levels must match its own datasheet and board wiring. Do not connect module TX to ESP GPIO43 based on old notes.
2. Set the ESP receiver via its **USB CDC console** at 115200 baud: `scanner-baud 9600`. This does not configure GM65. For the temporary `scan-local` test build, `scanner-test` requests the local display and `scanner-test stop` exits; these commands are not available in every released firmware. The test build uses a 256-byte frame buffer; longer data can split and non-printable bytes are not faithfully displayed.
3. If module settings are unknown and physical reconfiguration is authorized, use the original manual's setup-code enable and relevant labeled barcodes. Candidate labels and direct page links are below. This is **not a verified scan sequence**: preserve the prior configuration, check the module's responses, and do not reset to factory defaults merely to make a test pass.
4. Scan two distinct non-sensitive test codes; capture timestamps, count, byte lengths and exact expected/observed values privately. Confirm that each appears on the LCD without creating an MESFlow transaction, and verify baud, framing and any terminator separately. Only then mark the actual scanner/profile and exact setup sequence verified.

| Setting or operation | Manual reference | Current evidence |
|---|---|---|
| Enable setup codes | [PDF p. 8, printed p. 4, §1.4](https://www.sunrom.com/download/767.pdf#page=8) | Label present; manual payload extracted; physical scan **UNVERIFIED**. |
| TTL-232 / Series Output and 9600 bps | [PDF p. 9, printed p. 5, §2.1](https://www.sunrom.com/download/767.pdf#page=9) | Manual labels and default only; module setting and physical scan **UNVERIFIED**. |
| Continuous mode | [PDF p. 11, printed p. 7, §3.1](https://www.sunrom.com/download/767.pdf#page=11) | Optional candidate; actual mode and physical scan **UNVERIFIED**. Manual also describes manual mode; select per installation rather than assuming. |
| CR, TAB, CRLF or no tail | [PDF p. 23, printed p. 19, §6.4](https://www.sunrom.com/download/767.pdf#page=23) | Labels present; selected tail and physical scan **UNVERIFIED**. |
| Save / cancel | [PDF p. 8, printed p. 4, §1.5](https://www.sunrom.com/download/767.pdf#page=8) | Procedures vary by setting; consult the relevant manual section. Not observed. |

The manual's setup-code enable state and individual scanner firmware may differ. A visible barcode label or a beep is not proof that the resulting UART output is correct. Avoid interpreting example data codes as scanner settings.

## Recovery and evidence

Before changing a working scanner, record the known baud, interface, scan mode and suffix and retain its manual/configuration. If output disappears, first restore the previously documented receiver baud and scanner settings from the original manual; inspect power, shared ground and TX→RX. Factory reset changes multiple settings and is a last resort only when explicitly authorized and recoverable. Never claim a rollback or physical scan passed without testing it. Record firmware build/SHA, board identity, scanner model, date, barcode labels in order, setup acknowledgments and two real scans; redact payloads and credentials in shared logs.

Machine-readable status: [gm65-setup-profile.json](gm65-setup-profile.json). Update both files only when backed by physical evidence or a separately identified manual extraction.

## Manual-derived QR payloads (not hardware verified)

Hex encodes the exact bytes, including `80` and `2e`; it is not an ASCII command to send over ESP USB. Manual source SHA-256: `1ecb443b12bf8d6e3a58d7e5bf4e591e1035188de83154db64fa073674ec9e91`.

| Manual label | Payload hex |
|---|---|
| Default: setup code on | `80523033303230302e` |
| Series Output | `80303030303032302e` |
| 9600bps (Default) | `80573241333930312e` |
| Default Continuous Mode | `80523030303330322e` |
| Add tail CRLF | `80523630363132312e` |
| Save | `80533030303046462e` |

These manual-derived QR symbols have not been scanned or validated on the current GM65. The order setup on → Series Output → 9600 → selected scan mode → optional tail is a candidate procedure, not a verified manufacturer-mandated sequence; Save is used where the manual requires it, not assumed necessary for every setting. Scanner power/trigger, wiring, baud and double-scan evidence remain unverified. See [printable reference sheet](GM65_SETUP_SHEET.md) for source page links.
