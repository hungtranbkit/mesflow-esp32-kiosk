# esp-kiosk

Read and obey the workspace rules first:

`../AGENTS.md`

Project directory:
`esp-kiosk/`

Do not modify sibling projects unless the task explicitly requires a cross-project change.


## OTA agent onboarding (required)

Before any OTA, enrollment, device-flash, staging, or release task, read
[`docs/OTA_AGENT_HANDOFF.md`](docs/OTA_AGENT_HANDOFF.md). It is a dated
runbook, **not** a production release authorization.

If working on an authorized onsite machine, also read the local/private
`OTA_SITE_SETTINGS.local.md` under the ESP kiosk firmware workspace (if present).
Never copy private IPs, serial IDs, tokens, paths to secrets or credentials into Git.

Required evidence: exact repo SHAs; local vs production target; one-device
identity; backup + rollback readiness; actual OTA boot ACK; test exit codes.
`SUBMIT_CONFIRMED` is not evidence an agent executed a task. Distinguish
PASS, FAIL, BLOCKED and SKIPPED and do not merge/release on missing gates.
