const { test, expect } = require('@playwright/test');
const fs = require('fs');
const path = require('path');

const root = process.env.ESP_UI_AUDIT_DIR || path.resolve(__dirname, '../../../artifacts/esp-kiosk/ui-audit');
const report = JSON.parse(fs.readFileSync(path.join(root, 'report.json'), 'utf8'));

test.describe('ESP Kiosk real framebuffer states', () => {
  for (const screen of report.screens) {
    test(screen.screen, async ({ page }) => {
      await page.goto(`http://127.0.0.1:${process.env.ESP_UI_HARNESS_PORT || 8765}/esp-ui-test?state=${encodeURIComponent(screen.screen)}`);
      await expect(page.locator('#esp-screen')).toHaveJSProperty('naturalWidth', 240);
      await expect(page.locator('#esp-screen')).toHaveJSProperty('naturalHeight', 320);
      await expect(page.locator('#esp-screen')).toBeVisible();
      const box = await page.locator('#esp-screen').boundingBox();
      expect(box.width).toBe(240); expect(box.height).toBe(320);
      expect(screen.result).toBe('PASS');
      await page.screenshot({ path: path.join(root, 'playwright', `${screen.screen}.png`), fullPage: true });
    });
  }
});
