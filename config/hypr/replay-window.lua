-- Managed by Omarchy Replay: present the main history window above the desktop.
-- Dialogs retain their own size; the capture fixture has a different app ID.
hl.window_rule({
  match = { initial_class = "^omarchy-replay$", initial_title = "^(Replay|Omarchy Replay)$" },
  float = true,
  size = { "monitor_w*0.96", "monitor_h*0.94" },
  center = true,
  opacity = "1 override",
})
