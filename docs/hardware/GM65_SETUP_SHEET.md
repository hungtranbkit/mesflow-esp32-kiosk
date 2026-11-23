# GM65 setup — one-page reference

**Candidate only: no barcode below has been physically verified on this unit.** Use the linked original GROW V1.3 manual pages; this sheet does not reproduce its images. Do not scan Reset.

1. [Setup Code: “Default: setup code on”](https://www.sunrom.com/download/767.pdf#page=8), top left.
2. [“Series Output”](https://www.sunrom.com/download/767.pdf#page=9), upper center.
3. [“9600bps (Default)”](https://www.sunrom.com/download/767.pdf#page=9), right of the 1200/4800/9600 row.
4. If suitable for this unit: [“Default Continuous Mode”](https://www.sunrom.com/download/767.pdf#page=11), upper center.
5. Only if required: [“Add tail CRLF”](https://www.sunrom.com/download/767.pdf#page=23), right of the CR/TAB/CRLF group.
6. [“Save”](https://www.sunrom.com/download/767.pdf#page=8), left under §1.5, only where the manual's relevant operation requires it.

This ordering is a proposed workflow, not a proven scan sequence. Check model, wiring, supply, acknowledgments and two distinct scans; document each result. ESP USB console = 115200; scanner TX → GPIO44 RX, 9600 8N1, common ground; ESP UART TX unused. See [full instructions and limitations](GM65_SETUP_REUSE.md) and [manifest](gm65-setup-profile.json).
