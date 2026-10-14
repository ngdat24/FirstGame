extends Control
## Minimal Playable Godot UI for HUST Monopoly (Phase 2 Hotseat Prototype)
## Uses engine_client.gd exclusively: renders snapshot, enables only legal.actions,
## and sends actions over HTTP to the local C++ server.

const PLAYER_COLORS := [
	Color(0.90, 0.25, 0.25), # Player 1 - Red
	Color(0.20, 0.55, 0.95), # Player 2 - Blue
	Color(0.20, 0.75, 0.35), # Player 3 - Green
	Color(0.95, 0.70, 0.15), # Player 4 - Gold
]

var client: Node
var current_snap: Dictionary = {}
var tile_panels: Array[PanelContainer] = []
var tile_title_labels: Array[Label] = []
var tile_info_labels: Array[Label] = []
var tile_token_labels: Array[Label] = []

var status_label: Label
var player_cards: Array[RichTextLabel] = []
var actions_box: VBoxContainer
var bribe_slider: HSlider
var bribe_value_label: Label
var event_log: RichTextLabel
var new_game_btn: Button
var card_rules: Array = []
var card_modal: AcceptDialog
var tile_inspector: AcceptDialog
var turn_overlay: PanelContainer
var turn_overlay_label: Label
var last_turn_player := -2


func _ready() -> void:
	client = preload("res://engine_client.gd").new()
	add_child(client)
	_build_ui()
	var rules_resp: Dictionary = await client.get_rules()
	if rules_resp.get("ok", false):
		card_rules = rules_resp.get("cards", [])
	_start_new_game()


func _build_ui() -> void:
	var bg := ColorRect.new()
	bg.color = Color(0.10, 0.12, 0.15)
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)

	var root_hbox := HBoxContainer.new()
	root_hbox.set_anchors_preset(Control.PRESET_FULL_RECT)
	root_hbox.add_theme_constant_override("separation", 12)
	add_child(root_hbox)

	# --- Left: 11x11 Perimeter Board (40 tiles) ---
	var board_grid := GridContainer.new()
	board_grid.columns = 11
	board_grid.custom_minimum_size = Vector2(840, 840)
	board_grid.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	board_grid.size_flags_vertical = Control.SIZE_EXPAND_FILL
	root_hbox.add_child(board_grid)

	# Pre-create 40 tile widgets
	tile_panels.resize(40)
	tile_title_labels.resize(40)
	tile_info_labels.resize(40)
	tile_token_labels.resize(40)
	for i in range(40):
		var tile_id := i
		var p := PanelContainer.new()
		p.custom_minimum_size = Vector2(72, 72)
		p.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		p.size_flags_vertical = Control.SIZE_EXPAND_FILL
		var vb := VBoxContainer.new()
		vb.add_theme_constant_override("separation", 1)
		p.add_child(vb)

		var lbl_title := Label.new()
		lbl_title.add_theme_font_size_override("font_size", 10)
		lbl_title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		vb.add_child(lbl_title)

		var lbl_info := Label.new()
		lbl_info.add_theme_font_size_override("font_size", 10)
		lbl_info.modulate = Color(0.85, 0.85, 0.85)
		vb.add_child(lbl_info)

		var lbl_tokens := Label.new()
		lbl_tokens.add_theme_font_size_override("font_size", 11)
		lbl_tokens.modulate = Color(1, 0.95, 0.4)
		vb.add_child(lbl_tokens)

		tile_panels[i] = p
		tile_title_labels[i] = lbl_title
		tile_info_labels[i] = lbl_info
		tile_token_labels[i] = lbl_tokens
		p.mouse_filter = Control.MOUSE_FILTER_STOP
		p.gui_input.connect(func(event: InputEvent) -> void:
			if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
				_show_tile_inspector(tile_id)
		)

	for r in range(11):
		for c in range(11):
			var idx := _grid_to_tile_index(r, c)
			if idx >= 0:
				board_grid.add_child(tile_panels[idx])
			else:
				var spacer := Control.new()
				spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
				spacer.size_flags_vertical = Control.SIZE_EXPAND_FILL
				board_grid.add_child(spacer)

	# --- Right Sidebar: Status, 4 Players, Legal Actions, Event Log ---
	var sidebar := VBoxContainer.new()
	sidebar.custom_minimum_size = Vector2(480, 0)
	sidebar.size_flags_vertical = Control.SIZE_EXPAND_FILL
	sidebar.add_theme_constant_override("separation", 8)
	root_hbox.add_child(sidebar)

	var top_bar := HBoxContainer.new()
	sidebar.add_child(top_bar)

	status_label = Label.new()
	status_label.text = "Connecting to http://127.0.0.1:8080 ..."
	status_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	status_label.add_theme_font_size_override("font_size", 15)
	top_bar.add_child(status_label)

	new_game_btn = Button.new()
	new_game_btn.text = "New Game"
	new_game_btn.pressed.connect(_start_new_game)
	top_bar.add_child(new_game_btn)

	# 4 Player HUD cards (2x2 grid)
	var players_grid := GridContainer.new()
	players_grid.columns = 2
	players_grid.add_theme_constant_override("h_separation", 8)
	players_grid.add_theme_constant_override("v_separation", 6)
	sidebar.add_child(players_grid)

	for i in range(4):
		var card_panel := PanelContainer.new()
		card_panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		card_panel.custom_minimum_size = Vector2(230, 95)
		var rtl := RichTextLabel.new()
		rtl.bbcode_enabled = true
		rtl.fit_content = true
		rtl.scroll_active = false
		card_panel.add_child(rtl)
		players_grid.add_child(card_panel)
		player_cards.append(rtl)

	# Legal Actions Box
	var actions_title := Label.new()
	actions_title.text = "Legal Actions (Server-Driven):"
	actions_title.add_theme_font_size_override("font_size", 14)
	sidebar.add_child(actions_title)

	var actions_scroll := ScrollContainer.new()
	actions_scroll.custom_minimum_size = Vector2(0, 210)
	actions_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	sidebar.add_child(actions_scroll)

	actions_box = VBoxContainer.new()
	actions_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	actions_box.add_theme_constant_override("separation", 5)
	actions_scroll.add_child(actions_box)

	# Event Log Box
	var log_title := Label.new()
	log_title.text = "Event Log:"
	log_title.add_theme_font_size_override("font_size", 14)
	sidebar.add_child(log_title)

	event_log = RichTextLabel.new()
	event_log.bbcode_enabled = true
	event_log.scroll_following = true
	event_log.custom_minimum_size = Vector2(0, 180)
	event_log.size_flags_vertical = Control.SIZE_EXPAND_FILL
	sidebar.add_child(event_log)

	_create_dialogs()


func _create_dialogs() -> void:
	card_modal = AcceptDialog.new()
	card_modal.title = "Card drawn"
	card_modal.ok_text = "Continue"
	card_modal.min_size = Vector2(520, 240)
	add_child(card_modal)

	tile_inspector = AcceptDialog.new()
	tile_inspector.title = "Institute information"
	tile_inspector.ok_text = "Close"
	tile_inspector.min_size = Vector2(520, 300)
	add_child(tile_inspector)

	turn_overlay = PanelContainer.new()
	turn_overlay.set_anchors_preset(Control.PRESET_CENTER)
	turn_overlay.position = Vector2(-220, -90)
	turn_overlay.size = Vector2(440, 180)
	turn_overlay.z_index = 20
	turn_overlay.visible = false
	var overlay_box := VBoxContainer.new()
	overlay_box.alignment = BoxContainer.ALIGNMENT_CENTER
	overlay_box.add_theme_constant_override("separation", 12)
	turn_overlay.add_child(overlay_box)
	turn_overlay_label = Label.new()
	turn_overlay_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	turn_overlay_label.add_theme_font_size_override("font_size", 24)
	overlay_box.add_child(turn_overlay_label)
	var continue_btn := Button.new()
	continue_btn.text = "Continue"
	continue_btn.pressed.connect(func() -> void:
		turn_overlay.visible = false
	)
	overlay_box.add_child(continue_btn)
	add_child(turn_overlay)


## Maps an 11x11 perimeter cell (r, c) to tile index 0..39 clockwise starting from bottom-right (#0).
func _grid_to_tile_index(r: int, c: int) -> int:
	if r == 10:
		return 10 - c             # Bottom row: #0 (bottom-right) -> #10 (bottom-left)
	if c == 0:
		return 10 + (10 - r)      # Left column: #11 -> #20 (top-left)
	if r == 0:
		return 20 + c             # Top row: #21 -> #30 (top-right)
	if c == 10:
		return 30 + r             # Right column: #31 -> #39
	return -1


func _start_new_game() -> void:
	event_log.clear()
	_append_log("[color=yellow]Starting new game...[/color]")
	var resp: Dictionary = await client.new_game()
	_handle_response(resp)


func _send_player_action(action: Dictionary) -> void:
	var resp: Dictionary = await client.send_action(action)
	_handle_response(resp)


func _handle_response(resp: Dictionary) -> void:
	if not resp.get("ok", false):
		var err_code := str(resp.get("error", "error"))
		var err_msg := str(resp.get("message", "Unknown error"))
		status_label.text = "Error [%s]: %s" % [err_code, err_msg]
		_append_log("[color=red]ERROR (%s): %s[/color]" % [err_code, err_msg])
		return

	var events: Array = resp.get("events", [])
	for ev in events:
		_append_log("• " + str(ev))
		_show_card_from_event(str(ev))

	current_snap = resp.get("snapshot", {})
	_render_snapshot(current_snap)


func _render_snapshot(snap: Dictionary) -> void:
	if snap.is_empty():
		return

	var tempo := int(snap.get("tempo", 1))
	var max_tempo := int(snap.get("max_tempo", 64))
	var phase := str(snap.get("phase", ""))
	var cur_pid := int(snap.get("current_player_id", 0))
	var dice: Array = snap.get("last_dice", [0, 0])
	var game_over := bool(snap.get("game_over", false))

	if game_over:
		var wid := int(snap.get("winner_id", -1))
		status_label.text = "GAME OVER! Winner: Player %d (Tempo %d/%d)" % [wid + 1, tempo, max_tempo]
	else:
		status_label.text = "Tempo %d/%d | Turn: Player %d | Phase: %s | Dice: %d+%d" % [
			tempo, max_tempo, cur_pid + 1, phase, int(dice[0]), int(dice[1])
		]
		if cur_pid != last_turn_player:
			last_turn_player = cur_pid
			turn_overlay_label.text = "Player %d's turn" % (cur_pid + 1)
			turn_overlay.visible = true

	# Render 4 players
	var players: Array = snap.get("players", [])
	for i in range(min(4, players.size())):
		var p: Dictionary = players[i]
		var pid := int(p.get("id", i))
		var p_color: String = PLAYER_COLORS[pid].to_html(false)
		var turn_tag := " [b][<< TURN][/b]" if (pid == cur_pid and not game_over) else ""
		var status_tags: PackedStringArray = []
		if bool(p.get("is_bankrupt", false)):
			status_tags.append("[color=red]EXPELLED[/color]")
		if int(p.get("jail_turns", 0)) > 0:
			status_tags.append("Jail:%d" % int(p.get("jail_turns", 0)))
		if int(p.get("skip_turns", 0)) > 0:
			status_tags.append("Skip:%d" % int(p.get("skip_turns", 0)))
		if bool(p.get("halve_next_dice", false)):
			status_tags.append("1/2 Dice")
		if int(p.get("steal_bonus_percent", 0)) > 0:
			status_tags.append("+%d%% Steal" % int(p.get("steal_bonus_percent", 0)))

		var extra := (" | " + ", ".join(status_tags)) if status_tags.size() > 0 else ""
		player_cards[i].text = (
			"[color=#%s][b]%s[/b][/color]%s\n" % [p_color, str(p.get("name", "P")), turn_tag] +
			"Cash: [b]%s[/b] | Net: %s\n" % [_fmt(int(p.get("cash", 0))), _fmt(int(p.get("net_worth", 0)))] +
			"Tile: #%d | Owned: %d%s" % [int(p.get("position", 0)), (p.get("owned_tile_ids", []) as Array).size(), extra]
		)

	# Render 40 tiles
	var tiles: Array = snap.get("tiles", [])
	var pending_tid := int(snap.get("pending_tile_id", -1))
	for i in range(min(40, tiles.size())):
		var t: Dictionary = tiles[i]
		var tid := int(t.get("id", i))
		var t_name := str(t.get("name", ""))
		var t_type := str(t.get("type", ""))
		var owner_id := int(t.get("owner_id", -1))
		var grade := str(t.get("grade", "none"))

		tile_title_labels[i].text = "#%d %s" % [tid, t_name]
		if t_type == "institute":
			if owner_id >= 0:
				tile_info_labels[i].text = "P%d [%s] R:%dk" % [
					owner_id + 1, grade, int(t.get("rent", 0)) / 1000
				]
			else:
				tile_info_labels[i].text = "Buy: %dk" % [int(t.get("base_price", 0)) / 1000]
		else:
			tile_info_labels[i].text = "[%s]" % t_type

		# Highlight owner color or pending tile
		var style := StyleBoxFlat.new()
		style.bg_color = Color(0.16, 0.18, 0.22)
		if owner_id >= 0 and owner_id < 4:
			style.border_color = PLAYER_COLORS[owner_id]
			style.set_border_width_all(3)
		elif tid == pending_tid:
			style.border_color = Color(1, 1, 1)
			style.set_border_width_all(2)
		else:
			style.border_color = Color(0.3, 0.33, 0.38)
			style.set_border_width_all(1)
		tile_panels[i].add_theme_stylebox_override("panel", style)

		# Tokens on this tile
		var here: PackedStringArray = []
		for p in players:
			if not bool(p.get("is_bankrupt", false)) and int(p.get("position", -1)) == tid:
				here.append("P%d" % (int(p.get("id", 0)) + 1))
		tile_token_labels[i].text = " ".join(here)

	# Render Legal Action Buttons
	for child in actions_box.get_children():
		child.queue_free()

	var legal: Dictionary = snap.get("legal", {})
	var acts: Array = legal.get("actions", [])
	var min_bribe := int(legal.get("min_bribe", 0))
	var max_bribe := int(legal.get("max_bribe", 0))

	for a in acts:
		var act: Dictionary = a
		var a_type := str(act.get("type", ""))
		var tid := int(act.get("tile_id", -1))

		if a_type == "attempt_steal":
			var steal_box := VBoxContainer.new()
			actions_box.add_child(steal_box)

			bribe_slider = HSlider.new()
			bribe_slider.min_value = min_bribe
			bribe_slider.max_value = max_bribe
			bribe_slider.step = 1000
			bribe_slider.value = min_bribe
			steal_box.add_child(bribe_slider)

			var steal_btn := Button.new()
			steal_btn.text = "Attempt Steal (Bribe: %s VND)" % _fmt(min_bribe)
			bribe_slider.value_changed.connect(func(val: float) -> void:
				steal_btn.text = "Attempt Steal (Bribe: %s VND)" % _fmt(int(val))
			)
			steal_btn.pressed.connect(func() -> void:
				var payload := act.duplicate()
				payload["amount"] = int(bribe_slider.value)
				_send_player_action(payload)
			)
			steal_box.add_child(steal_btn)
		else:
			var btn := Button.new()
			btn.text = _format_action_button(a_type, tid, tiles)
			btn.pressed.connect(func() -> void:
				_send_player_action(act)
			)
			actions_box.add_child(btn)


func _show_tile_inspector(tile_id: int) -> void:
	var current_tiles: Array = current_snap.get("tiles", [])
	if tile_id < 0 or tile_id >= current_tiles.size():
		return
	var t: Dictionary = current_tiles[tile_id]
	if str(t.get("type", "")) != "institute":
		tile_inspector.title = "Tile information"
		tile_inspector.dialog_text = "#%d %s\nThis tile has no Institute upgrades." % [
			tile_id, str(t.get("name", ""))
		]
		tile_inspector.popup_centered()
		return
	var base := int(t.get("base_price", 0))
	var upgrade := int(t.get("upgrade_cost", 0))
	var grade := str(t.get("grade", "none"))
	var owner := int(t.get("owner_id", -1))
	var lines := [
		"#%d %s" % [tile_id, str(t.get("name", ""))],
		"Current owner: %s | Grade: %s" % [
			("Player %d" % (owner + 1)) if owner >= 0 else "Unowned", grade
		],
		"",
		"Price / upgrade cost:",
		"D: %s / %s" % [_fmt(base), _fmt(upgrade)],
		"C: %s / %s" % [_fmt(base + upgrade), _fmt(upgrade)],
		"B: %s / %s" % [_fmt(base + upgrade * 2), _fmt(upgrade)],
		"A: %s" % _fmt(base + upgrade * 3),
		"",
		"Current rent: %s VND" % _fmt(int(t.get("rent", 0))),
		"Liquidation value: %s VND" % _fmt(int(t.get("sell_value", 0)))
	]
	tile_inspector.title = "Institute inspector"
	tile_inspector.dialog_text = "\n".join(lines)
	tile_inspector.popup_centered()


func _show_card_from_event(event: String) -> void:
	var marker := "draws \""
	var start := event.find(marker)
	if start < 0:
		return
	start += marker.length()
	var finish := event.find("\"", start)
	if finish < 0:
		return
	var title := event.substr(start, finish - start)
	for card in card_rules:
		if str(card.get("title", "")) == title:
			card_modal.title = "Card drawn: " + title
			card_modal.dialog_text = "%s\n\nEffect: %s" % [
				str(card.get("description", "")), str(card.get("effect", ""))
			]
			card_modal.popup_centered()
			return


func _format_action_button(a_type: String, tid: int, tiles: Array) -> String:
	var t_name := ("#%d %s" % [tid, str(tiles[tid].get("name", ""))]) if (tid >= 0 and tid < tiles.size()) else ""
	match a_type:
		"roll_dice":
			return "🎲 Roll Dice"
		"pay_retake_fee":
			return "💸 Pay Retake Fee (Leave Jail)"
		"buy":
			return "🏛️ Buy Institute"
		"upgrade":
			return "📈 Upgrade Grade"
		"decline":
			return "⏭️ Skip / Decline"
		"pay_rent":
			return "💰 Pay Tuition / Rent"
		"teleport_unowned":
			return "🚀 Teleport to Unowned: " + t_name
		"teleport_owned":
			return "🏠 Teleport to Owned: " + t_name
		"sell_tile":
			var sell_val := int(tiles[tid].get("sell_value", 0)) if (tid >= 0 and tid < tiles.size()) else 0
			return "🔨 Liquidate %s (+%s VND)" % [t_name, _fmt(sell_val)]
		_:
			return a_type + (" " + t_name if not t_name.is_empty() else "")


func _fmt(n: int) -> String:
	var s := str(abs(n))
	var out := ""
	for i in range(s.length()):
		if i > 0 and (s.length() - i) % 3 == 0:
			out += ","
		out += s[i]
	return ("-" + out) if n < 0 else out


func _append_log(line: String) -> void:
	event_log.append_text(line + "\n")
