import assert from "node:assert/strict";

// Browser interaction evidence, not a claim of human assistive-technology review.
export async function inspectKeyboardAccess(page, name, report) {
  async function tabTo(index, reverse = false) {
    // Chromium date/time inputs expose several native keyboard segments while
    // document.activeElement remains the same input. Bound that traversal.
    for (let segment = 0; segment < 5; segment++) {
      await page.keyboard.press(reverse ? "Shift+Tab" : "Tab");
      const state = await page.evaluate(i => ({
        reached: document.activeElement === window.__keyboardTargets[i],
        nativeSegment: document.activeElement?.matches("input[type=date],input[type=time],input[type=datetime-local]"),
      }), index);
      if (state.reached || !state.nativeSegment) return;
    }
  }
  for (const width of [320, 1280]) {
    await page.setViewportSize({ width, height: 900 });
    const targetCount = await page.evaluate(() => {
      const targets = [...document.querySelectorAll("a[href],button,input:not([type=hidden]),select,textarea,[tabindex]")]
        .filter(node => node.tabIndex >= 0 && !node.disabled && node.getClientRects().length && getComputedStyle(node).visibility !== "hidden");
      if (targets.some(node => node.tabIndex > 0)) throw new Error("Positive tabindex disrupts reading order");
      window.__keyboardTargets = targets;
      document.body.setAttribute("tabindex", "-1");
      document.body.focus();
      document.body.removeAttribute("tabindex");
      return targets.length;
    });
    assert(targetCount > 0 && targetCount < 80, "keyboard route has an unexpected target count");
    for (let index = 0; index < targetCount; index++) {
      await tabTo(index);
      const result = await page.evaluate(i => {
        const target = window.__keyboardTargets[i];
        const rect = target.getBoundingClientRect();
        const css = getComputedStyle(target);
        return { matches: document.activeElement === target,
          visible: rect.left >= -1 && rect.right <= innerWidth + 1 && rect.top >= -1 && rect.bottom <= innerHeight + 1,
          outline: css.outlineStyle !== "none" && parseFloat(css.outlineWidth) >= 2 };
      }, index);
      assert.equal(result.matches, true, `Keyboard order failed on ${name}/${width}/${index}`);
      assert.equal(result.visible, true, `Focused control is clipped on ${name}/${width}/${index}`);
      assert.equal(result.outline, true, `Focus indicator missing on ${name}/${width}/${index}`);
    }
    for (let index = targetCount - 2; index >= 0; index--) {
      await tabTo(index, true);
      assert.equal(await page.evaluate(i => document.activeElement === window.__keyboardTargets[i], index), true,
        `Reverse keyboard order failed on ${name}/${width}/${index}`);
    }
    if (await page.locator("a.skip-link").count()) {
      await page.keyboard.press("Enter");
      assert.equal(await page.locator("main").evaluate(node => node === document.activeElement), true, "skip link focuses main content");
    }
    const reducedMotion = await page.evaluate(() => matchMedia("(prefers-reduced-motion: reduce)").matches &&
      document.getAnimations().every(animation => animation.playState !== "running"));
    assert.equal(reducedMotion, true, `Reduced motion was not respected on ${name}`);
    await page.evaluate(() => { delete window.__keyboardTargets; });
    report.keyboard.push({ name, width, targets: targetCount, forward: true, reverse: true, visibleFocus: true, reducedMotion });
  }
}
