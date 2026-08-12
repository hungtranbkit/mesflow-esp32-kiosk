# ESP32 UI Capture Report

Firmware version: ESP32-KIOSK-5.1.9-WORKER-QTY-FLOW  
Resolution: 240x320  
ESP URL: http://192.168.1.115:17892  
Capture time: 2026-08-09T18:39:21+07:00  

## Screens

| Screen | Screenshot | State | Result |
|---|---|---|---|
| ready | [01_ready.png](normal/01_ready.png) | READY | PASS |
| scan_employee | [02_scan_employee.png](normal/02_scan_employee.png) | READY | PASS |
| employee_ok | [03_employee_ok.png](normal/03_employee_ok.png) | WORKER_OK | PASS |
| scan_operation | [04_scan_operation.png](normal/04_scan_operation.png) | WORKER_OK | PASS |
| operation_ok | [05_operation_ok.png](normal/05_operation_ok.png) | OPERATION_OK | PASS |
| working | [06_working.png](normal/06_working.png) | START_SUCCESS | PASS |
| input_good | [07_input_good.png](normal/07_input_good.png) | INPUT_GOOD | PASS |
| input_defect | [08_input_defect.png](normal/08_input_defect.png) | INPUT_DEFECT | PASS |
| ask_rework | [09_ask_rework.png](normal/09_ask_rework.png) | ASK_REWORK | PASS |
| input_rework | [10_input_rework.png](normal/10_input_rework.png) | INPUT_REWORK | PASS |
| confirm_qty | [11_confirm_qty.png](normal/11_confirm_qty.png) | CONFIRM_QTY | PASS |
| finish_success | [12_finish_success.png](normal/12_finish_success.png) | FINISH_SUCCESS | PASS |
| error_state | [13_error_state.png](error/13_error_state.png) | ERROR_STATE | PASS |
| employee_short_name | [14_employee_short_name.png](normal/14_employee_short_name.png) | WORKER_OK | PASS |
| employee_long_name | [15_employee_long_name.png](long-text/15_employee_long_name.png) | WORKER_OK | PASS |
| operation_short_name | [16_operation_short_name.png](normal/16_operation_short_name.png) | OPERATION_OK | PASS |
| operation_long_name | [17_operation_long_name.png](long-text/17_operation_long_name.png) | OPERATION_OK | PASS |
| confirm_normal | [18_confirm_normal.png](normal/18_confirm_normal.png) | CONFIRM_QTY | PASS |
| confirm_large_numbers | [19_confirm_large_numbers.png](large-number/19_confirm_large_numbers.png) | CONFIRM_QTY | PASS |
| repairable_zero | [20_repairable_zero.png](normal/20_repairable_zero.png) | ASK_REWORK | PASS |
| repairable_nonzero | [21_repairable_nonzero.png](normal/21_repairable_nonzero.png) | ASK_REWORK | PASS |
| error_short | [22_error_short.png](error/22_error_short.png) | ERROR_STATE | PASS |
| error_long | [23_error_long.png](long-text/23_error_long.png) | ERROR_STATE | PASS |
| offline | [24_offline.png](normal/24_offline.png) | OFFLINE | PASS |
| maintenance | [25_maintenance.png](normal/25_maintenance.png) | MAINTENANCE | PASS |

## Checks

- Screenshot API: PASS
- Resolution: PASS
- BMP decode: PASS
- UI state endpoint: PASS
- Long text variants: PASS
- Large number variants: PASS

Captured: 25/25 PASS.
