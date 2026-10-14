extends Control
## Upgraded Playable Godot UI for HUST Monopoly (Phase 2 Hotseat Prototype)
## Fixes:
## 1. No annoying "Continue" popup blocking every turn or card draw.
## 2. Visual 2D Dice Box + Center Dashboard + Tile Inspector in the middle of the board.
## 3. Full Bribe controller (Slider + SpinBox + Quick 25%/50%/75%/100% buttons) showing live Steal Success %.

const PLAYER_COLORS := [
	Color(0.92, 0.28, 0.28), # Player 1 - Red
	Color(0.22, 0.58, 0.98), # Player 2 - Blue
	Color(0.22, 0.78, 0.38), # Player 3 - Green
	Color(0.96, 0.72, 0.16), # Player 4 - Gold
]

const DICE_FACES := ["—", "⚀ 1", "⚁ 2", "⚂ 3", "⚃ 4", "⚄ 5", "⚅ 6"]

var client: Node
var current_snap: Dictionary = {}
var selected_tile_id: int = 0

var tile_panels: Array[PanelContainer] = []
var tile_title_labels: Array[Label] = []
var tile_info_labels: Array[Label] = []
var tile_token_labels: Array[Label] = []

var status_label: Label
var player_cards: Array[RichTextLabel] = []
var player_panels: Array[PanelContainer] = []
var actions_box: VBoxContainer
var event_log: RichTextLabel

# Center Board Stage widgets
var turn_banner_label: Label
var dice_left_label: Label
var dice_right_label: Label
var dice_sum_label: Label
var latest_event_banner: RichTextLabel
var tile_inspector_label: RichTextLabel


func _ready() -> void:
	client = preload("res://engine_client.gd").new()
	add_child(client)
	_build_ui()
	await get_tree().process_frame
	_start_new_game()


func _build_ui() -> void:
	var bg := ColorRect.new()
	bg.color = Color(0.09, 0.11, 0.14)
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)

	var root_hbox := HBoxContainer.new()
	root_hbox.set_anchors_preset(Control.PRESET_FULL_RECT)
	root_hbox.add_theme_constant_override("separation", 10)
	add_child(root_hbox)

	# --- Left: Board Area (11x11 perimeter + Center Dashboard overlay) ---
	var board_container := Control.new()
	board_container.custom_minimum_size = Vector2(850, 850)
	board_container.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	board_container.size_flags_vertical = Control.SIZE_EXPAND_FILL
	root_hbox.add_child(board_container)

	var board_grid := GridContainer.new()
	board_grid.columns = 11
	board_grid.set_anchors_preset(Control.PRESET_FULL_RECT)
	board_grid.add_theme_constant_override("h_separation", 2)
	board_grid.add_theme_constant_override("v_separation", 2)
	board_container.add_child(board_grid)

	tile_panels.resize(40)
	tile_title_labels.resize(40)
	tile_info_labels.resize(40)
	tile_token_labels.resize(40)

	for i in range(40):
		var p := PanelContainer.new()
		p.custom_minimum_size = Vector2(72, 72)
		p.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		p.size_flags_vertical = Control.SIZE_EXPAND_FILL
		p.mouse_filter = Control.MOUSE_FILTER_STOP
		var tile_idx := i
		p.gui_input.connect(func(event: InputEvent) -> void:
			if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
				selected_tile_id = tile_idx
				_update_tile_inspector()
		)

		var vb := VBoxContainer.new()
		vb.add_theme_constant_override("separation", 1)
		p.add_child(vb)

		var lbl_title := Label.new()
		lbl_title.add_theme_font_size_override("font_size", 10)
		lbl_title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		vb.add_child(lbl_title)

		var lbl_info := Label.new()
		lbl_info.add_theme_font_size_override("font_size", 10)
		lbl_info.modulate = Color(0.85, 0.90, 0.95)
		vb.add_child(lbl_info)

		var lbl_tokens := Label.new()
		lbl_tokens.add_theme_font_size_override("font_size", 11)
		lbl_tokens.modulate = Color(1.0, 0.95, 0.35)
		vb.add_child(lbl_tokens)

		tile_panels[i] = p
		tile_title_labels[i] = lbl_title
		tile_info_labels[i] = lbl_info
		tile_token_labels[i] = lbl_tokens

	for r in range(11):
		for c in range(11):
			var idx := _grid_to_tile_index(r, c)
			if idx >= 0:
				board_grid.add_child(tile_panels[idx])
			else:
				var spacer := Control.new()
				spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
				spacer.size_flags_vertical = Control.SIZE_EXPAND_FILL
				spacer.mouse_filter = Control.MOUSE_FILTER_IGNORE
				board_grid.add_child(spacer)

	# --- Center Hub inside the 9x9 hollow interior of the 11x11 board ---
	var center_panel := PanelContainer.new()
	center_panel.anchor_left = 0.12
	center_panel.anchor_top = 0.12
	center_panel.anchor_right = 0.88
	center_panel.anchor_bottom = 0.88
	var center_style := StyleBoxFlat.new()
	center_style.bg_color = Color(0.12, 0.15, 0.19, 0.96)
	center_style.border_color = Color(0.28, 0.34, 0.42)
	center_style.set_border_width_all(2)
	center_style.set_corner_radius_all(8)
	center_style.set_content_margin_all(14)
	center_panel.add_theme_stylebox_override("panel", center_style)
	board_container.add_child(center_panel)

	var center_vbox := VBoxContainer.new()
	center_vbox.add_theme_constant_override("separation", 10)
	center_panel.add_child(center_vbox)

	turn_banner_label = Label.new()
	turn_banner_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	turn_banner_label.add_theme_font_size_override("font_size", 20)
	turn_banner_label.text = "HUST MONOPOLY"
	center_vbox.add_child(turn_banner_label)

	# Visual Dice Box
	var dice_hbox := HBoxContainer.new()
	dice_hbox.alignment = BoxContainer.ALIGNMENT_CENTER
	dice_hbox.add_theme_constant_override("separation", 16)
	center_vbox.add_child(dice_hbox)

	dice_left_label = _make_die_box()
	dice_right_label = _make_die_box()
	dice_hbox.add_child(dice_left_label.get_parent())
	dice_hbox.add_child(dice_right_label.get_parent())

	dice_sum_label = Label.new()
	dice_sum_label.add_theme_font_size_override("font_size", 18)
	dice_sum_label.text = "Roll to start"
	dice_hbox.add_child(dice_sum_label)

	# Non-blocking Last Action / Card Banner
	latest_event_banner = RichTextLabel.new()
	latest_event_banner.bbcode_enabled = true
	latest_event_banner.fit_content = true
	latest_event_banner.custom_minimum_size = Vector2(0, 70)
	center_vbox.add_child(latest_event_banner)

	# Tile Inspector (click any tile on the board to inspect)
	tile_inspector_label = RichTextLabel.new()
	tile_inspector_label.bbcode_enabled = true
	tile_inspector_label.size_flags_vertical = Control.SIZE_EXPAND_FILL
	center_vbox.add_child(tile_inspector_label)

	# --- Right Sidebar: Status, 4 Players, Legal Actions + Bribe Calculator, Event Log ---
	var sidebar := VBoxContainer.new()
	sidebar.custom_minimum_size = Vector2(470, 0)
	sidebar.size_flags_vertical = Control.SIZE_EXPAND_FILL
	sidebar.add_theme_constant_override("separation", 8)
	root_hbox.add_child(sidebar)

	var top_bar := HBoxContainer.new()
	sidebar.add_child(top_bar)

	status_label = Label.new()
	status_label.text = "Connecting..."
	status_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	status_label.add_theme_font_size_override("font_size", 14)
	top_bar.add_child(status_label)

	var new_game_btn := Button.new()
	new_game_btn.text = "🔄 New Game"
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
		card_panel.custom_minimum_size = Vector2(225, 92)
		var rtl := RichTextLabel.new()
		rtl.bbcode_enabled = true
		rtl.fit_content = true
		rtl.scroll_active = false
		card_panel.add_child(rtl)
		players_grid.add_child(card_panel)
		player_panels.append(card_panel)
		player_cards.append(rtl)

	var actions_title := Label.new()
	actions_title.text = "Actions:"
	actions_title.add_theme_font_size_override("font_size", 14)
	sidebar.add_child(actions_title)

	var actions_scroll := ScrollContainer.new()
	actions_scroll.custom_minimum_size = Vector2(0, 250)
	actions_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	sidebar.add_child(actions_scroll)

	actions_box = VBoxContainer.new()
	actions_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	actions_box.add_theme_constant_override("separation", 6)
	actions_scroll.add_child(actions_box)

	var log_title := Label.new()
	log_title.text = "Full Event Log:"
	log_title.add_theme_font_size_override("font_size", 13)
	sidebar.add_child(log_title)

	event_log = RichTextLabel.new()
	event_log.bbcode_enabled = true
	event_log.scroll_following = true
	event_log.custom_minimum_size = Vector2(0, 170)
	event_log.size_flags_vertical = Control.SIZE_EXPAND_FILL
	sidebar.add_child(event_log)


func _make_die_box() -> Label:
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(76, 56)
	var st := StyleBoxFlat.new()
	st.bg_color = Color(0.20, 0.24, 0.30)
	st.border_color = Color(0.55, 0.65, 0.78)
	st.set_border_width_all(2)
	st.set_corner_radius_all(6)
	panel.add_theme_stylebox_override("panel", st)

	var lbl := Label.new()
	lbl.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	lbl.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	lbl.add_theme_font_size_override("font_size", 24)
	lbl.text = "—"
	panel.add_child(lbl)
	return lbl


func _grid_to_tile_index(r: int, c: int) -> int:
	if r == 10:
		return 10 - c
	if c == 0:
		return 10 + (10 - r)
	if r == 0:
		return 20 + c
	if c == 10:
		return 30 + r
	return -1


func _start_new_game() -> void:
	event_log.clear()
	_append_log("[color=yellow]=== Starting New Game ===[/color]")
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
	var recent_lines: PackedStringArray = []
	for ev in events:
		var line := str(ev)
		_append_log("• " + line)
		recent_lines.append("• " + line)

	if recent_lines.size() > 0:
		latest_event_banner.text = "[b][color=#ffd966]Latest Events:[/color][/b]\n" + "\n".join(recent_lines)
	else:
		latest_event_banner.text = ""

	current_snap = resp.get("snapshot", {})
	var cur_pid := int(current_snap.get("current_player_id", 0))
	var players: Array = current_snap.get("players", [])
	if cur_pid >= 0 and cur_pid < players.size():
		selected_tile_id = int(players[cur_pid].get("position", 0))
	var pending_tid := int(current_snap.get("pending_tile_id", -1))
	if pending_tid >= 0:
		selected_tile_id = pending_tid

	_render_snapshot(current_snap)


func _render_snapshot(snap: Dictionary) -> void:
	if snap.is_empty():
		return

	var tempo := int(snap.get("tempo", 1))
	var max_tempo := int(snap.get("max_tempo", 64))
	var phase := str(snap.get("phase", ""))
	var cur_pid := int(snap.get("current_player_id", 0))
	var dice: Array = snap.get("last_dice", [0, 0])
	var d1 := int(dice[0])
	var d2 := int(dice[1])
	var game_over := bool(snap.get("game_over", false))

	# Update visual Dice Box
	dice_left_label.text = DICE_FACES[clampi(d1, 0, 6)]
	dice_right_label.text = DICE_FACES[clampi(d2, 0, 6)]
	if d1 > 0 and d2 > 0:
		var dbl := " (DOUBLES!)" if d1 == d2 else ""
		dice_sum_label.text = "= %d%s" % [d1 + d2, dbl]
	else:
		dice_sum_label.text = "Ready to roll"

	if game_over:
		var wid := int(snap.get("winner_id", -1))
		status_label.text = "🏆 GAME OVER! Winner: Player %d (Tempo %d/%d)" % [wid + 1, tempo, max_tempo]
		turn_banner_label.text = "🏆 WINNER: PLAYER %d 🏆" % [wid + 1]
	else:
		status_label.text = "Tempo %d/%d  |  Phase: %s" % [tempo, max_tempo, phase]
		turn_banner_label.text = "▶ LƯỢT CỦA PLAYER %d  (Tempo %d/%d)" % [cur_pid + 1, tempo, max_tempo]
		turn_banner_label.modulate = PLAYER_COLORS[clampi(cur_pid, 0, 3)]

	# Render 4 players
	var players: Array = snap.get("players", [])
	for i in range(min(4, players.size())):
		var p: Dictionary = players[i]
		var pid := int(p.get("id", i))
		var p_color: String = PLAYER_COLORS[pid].to_html(false)
		var is_turn := (pid == cur_pid and not game_over)
		var turn_tag := " [b][color=yellow]◀ ĐANG ĐI[/color][/b]" if is_turn else ""

		var p_style := StyleBoxFlat.new()
		p_style.bg_color = Color(0.15, 0.18, 0.22)
		p_style.border_color = PLAYER_COLORS[pid] if is_turn else Color(0.28, 0.31, 0.36)
		p_style.set_border_width_all(3 if is_turn else 1)
		p_style.set_corner_radius_all(5)
		p_style.set_content_margin_all(6)
		player_panels[i].add_theme_stylebox_override("panel", p_style)

		var status_tags: PackedStringArray = []
		if bool(p.get("is_bankrupt", false)):
			status_tags.append("[color=red]BỊ ĐUỔI HỌC[/color]")
		if int(p.get("jail_turns", 0)) > 0:
			status_tags.append("Thi lại:%d" % int(p.get("jail_turns", 0)))
		if int(p.get("skip_turns", 0)) > 0:
			status_tags.append("Mất lượt:%d" % int(p.get("skip_turns", 0)))
		if bool(p.get("halve_next_dice", false)):
			status_tags.append("1/2 Xúc xắc")
		if int(p.get("steal_bonus_percent", 0)) > 0:
			status_tags.append("+%d%% Phao thi" % int(p.get("steal_bonus_percent", 0)))

		var extra := (" | " + ", ".join(status_tags)) if status_tags.size() > 0 else ""
		player_cards[i].text = (
			"[color=#%s][b]%s[/b][/color]%s\n" % [p_color, str(p.get("name", "P")), turn_tag] +
			"Tiền: [b]%s[/b] | TS: %s\n" % [_fmt(int(p.get("cash", 0))), _fmt(int(p.get("net_worth", 0)))] +
			"Ô: #%d | Số viện: %d%s" % [int(p.get("position", 0)), (p.get("owned_tile_ids", []) as Array).size(), extra]
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
				tile_info_labels[i].text = "P%d [%s] Phạt:%dk" % [
					owner_id + 1, grade, int(t.get("rent", 0)) / 1000
				]
			else:
				tile_info_labels[i].text = "Mua: %dk" % [int(t.get("base_price", 0)) / 1000]
		else:
			tile_info_labels[i].text = "[%s]" % t_type

		var style := StyleBoxFlat.new()
		style.bg_color = Color(0.16, 0.19, 0.24) if tid != pending_tid else Color(0.24, 0.28, 0.36)
		if owner_id >= 0 and owner_id < 4:
			style.border_color = PLAYER_COLORS[owner_id]
			style.set_border_width_all(3)
		elif tid == pending_tid:
			style.border_color = Color(1, 1, 1)
			style.set_border_width_all(2)
		else:
			style.border_color = Color(0.30, 0.33, 0.38)
			style.set_border_width_all(1)
		style.set_content_margin_all(3)
		tile_panels[i].add_theme_stylebox_override("panel", style)

		var here: PackedStringArray = []
		for p in players:
			if not bool(p.get("is_bankrupt", false)) and int(p.get("position", -1)) == tid:
				here.append("●P%d" % (int(p.get("id", 0)) + 1))
		tile_token_labels[i].text = " ".join(here)

	_update_tile_inspector()
	_render_legal_actions(snap, tiles, players, cur_pid, pending_tid)


func _update_tile_inspector() -> void:
	var tiles: Array = current_snap.get("tiles", [])
	if selected_tile_id < 0 or selected_tile_id >= tiles.size():
		return
	var t: Dictionary = tiles[selected_tile_id]
	var tid := int(t.get("id", selected_tile_id))
	var t_name := str(t.get("name", ""))
	var t_type := str(t.get("type", ""))
	var owner_id := int(t.get("owner_id", -1))
	var owner_text := ("Player %d" % (owner_id + 1)) if owner_id >= 0 else "Chưa ai sở hữu"

	if t_type == "institute":
		tile_inspector_label.text = (
			"[b][color=#9fc5e8]🔍 Chi tiết Ô #%d: %s[/color][/b] (Click ô bất kỳ để xem)\n" % [tid, t_name] +
			"• Chủ sở hữu: [b]%s[/b]  |  Điểm hiện tại: [b]%s[/b]\n" % [owner_text, str(t.get("grade", "none"))] +
			"• Giá mua gốc: [b]%s VND[/b]  |  Phí nâng cấp: [b]%s VND[/b]\n" % [_fmt(int(t.get("base_price", 0))), _fmt(int(t.get("upgrade_cost", 0)))] +
			"• Tổng vốn đã đầu tư (Mốc tính 100%% cướp): [b]%s VND[/b]\n" % _fmt(int(t.get("total_invested", 0))) +
			"• Tiền học liệu (Rent hiện tại): [b][color=#ff9999]%s VND[/color][/b]  |  Giá thanh lý (50%%): [b]%s VND[/b]" % [
				_fmt(int(t.get("rent", 0))), _fmt(int(t.get("sell_value", 0)))
			]
		)
	else:
		tile_inspector_label.text = (
			"[b][color=#9fc5e8]🔍 Chi tiết Ô #%d: %s[/color][/b]\n" % [tid, t_name] +
			"• Loại ô đặc biệt: [b]%s[/b]" % t_type
		)


func _render_legal_actions(snap: Dictionary, tiles: Array, players: Array, cur_pid: int, pending_tid: int) -> void:
	for child in actions_box.get_children():
		child.queue_free()

	var legal: Dictionary = snap.get("legal", {})
	var acts: Array = legal.get("actions", [])
	var min_bribe := int(legal.get("min_bribe", 1))
	var max_bribe := int(legal.get("max_bribe", 1))

	for a in acts:
		var act: Dictionary = a
		var a_type := str(act.get("type", ""))
		var tid := int(act.get("tile_id", -1))

		if a_type == "attempt_steal":
			_build_steal_controls(act, min_bribe, max_bribe, tiles, players, cur_pid, pending_tid)
		else:
			var btn := Button.new()
			btn.custom_minimum_size = Vector2(0, 38)
			btn.text = _format_action_button(a_type, tid, tiles, pending_tid)
			btn.pressed.connect(func() -> void:
				_send_player_action(act)
			)
			actions_box.add_child(btn)


func _build_steal_controls(act: Dictionary, min_bribe: int, max_bribe: int, tiles: Array, players: Array, cur_pid: int, pending_tid: int) -> void:
	var total_inv := 1
	if pending_tid >= 0 and pending_tid < tiles.size():
		total_inv = maxi(1, int(tiles[pending_tid].get("total_invested", 1)))
	var bonus_pct := 0
	if cur_pid >= 0 and cur_pid < players.size():
		bonus_pct = int(players[cur_pid].get("steal_bonus_percent", 0))

	var panel := PanelContainer.new()
	var st := StyleBoxFlat.new()
	st.bg_color = Color(0.20, 0.16, 0.24)
	st.border_color = Color(0.75, 0.45, 0.95)
	st.set_border_width_all(2)
	st.set_corner_radius_all(6)
	st.set_content_margin_all(8)
	panel.add_theme_stylebox_override("panel", st)
	actions_box.add_child(panel)

	var vbox := VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 6)
	panel.add_child(vbox)

	var default_bribe := clampi(total_inv / 2, min_bribe, max_bribe)

	var info_lbl := Label.new()
	info_lbl.add_theme_font_size_override("font_size", 13)
	vbox.add_child(info_lbl)

	var hbox_input := HBoxContainer.new()
	hbox_input.add_theme_constant_override("separation", 8)
	vbox.add_child(hbox_input)

	var slider := HSlider.new()
	slider.min_value = min_bribe
	slider.max_value = max_bribe
	slider.step = 1
	slider.value = default_bribe
	slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	hbox_input.add_child(slider)

	var spin := SpinBox.new()
	spin.min_value = min_bribe
	spin.max_value = max_bribe
	spin.step = 5000
	spin.value = default_bribe
	spin.custom_minimum_size = Vector2(120, 0)
	hbox_input.add_child(spin)

	# Quick preset buttons: 25%, 50%, 75%, 100% chance
	var quick_hbox := HBoxContainer.new()
	quick_hbox.add_theme_constant_override("separation", 6)
	vbox.add_child(quick_hbox)

	var steal_btn := Button.new()
	steal_btn.custom_minimum_size = Vector2(0, 40)

	var update_steal_ui := func(bribe_val: int) -> void:
		var raw_pct: float = (float(bribe_val) / float(total_inv)) * 100.0 + float(bonus_pct)
		var win_pct: float = clampf(raw_pct, 0.0, 100.0)
		var bonus_note := (" (đã cộng +%d%% Phao thi)" % bonus_pct) if bonus_pct > 0 else ""
		info_lbl.text = "🕵️ Cược Thi Hộ (Vốn ô đất: %s VND)%s" % [_fmt(total_inv), bonus_note]
		steal_btn.text = "🔥 Cược %s VND  ➔  Tỷ lệ cướp thành công: %.1f%%" % [_fmt(bribe_val), win_pct]

	for target_pct in [25, 50, 75, 100]:
		var qbtn := Button.new()
		qbtn.text = "%d%%" % target_pct
		qbtn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		qbtn.pressed.connect(func() -> void:
			var needed_pct := maxi(0, target_pct - bonus_pct)
			var target_bribe := int(ceil(float(total_inv) * float(needed_pct) / 100.0))
			target_bribe = clampi(target_bribe, min_bribe, max_bribe)
			slider.value = target_bribe
			spin.value = target_bribe
			update_steal_ui.call(target_bribe)
		)
		quick_hbox.add_child(qbtn)

	slider.value_changed.connect(func(v: float) -> void:
		var iv := clampi(int(v), min_bribe, max_bribe)
		if int(spin.value) != iv:
			spin.set_value_no_signal(iv)
		update_steal_ui.call(iv)
	)
	spin.value_changed.connect(func(v: float) -> void:
		var iv := clampi(int(v), min_bribe, max_bribe)
		if int(slider.value) != iv:
			slider.set_value_no_signal(iv)
		update_steal_ui.call(iv)
	)

	update_steal_ui.call(default_bribe)

	steal_btn.pressed.connect(func() -> void:
		var payload := act.duplicate()
		payload["amount"] = clampi(int(spin.value), min_bribe, max_bribe)
		_send_player_action(payload)
	)
	vbox.add_child(steal_btn)


func _format_action_button(a_type: String, tid: int, tiles: Array, pending_tid: int) -> String:
	var t_name := ("#%d %s" % [tid, str(tiles[tid].get("name", ""))]) if (tid >= 0 and tid < tiles.size()) else ""
	var p_tile: Dictionary = tiles[pending_tid] if (pending_tid >= 0 and pending_tid < tiles.size()) else {}
	match a_type:
		"roll_dice":
			return "🎲 Đổ Xúc Xắc (Roll Dice)"
		"pay_retake_fee":
			return "💸 Nộp 50,000 VND Phí Thi Lại (Ra tù ngay)"
		"buy":
			return "🏛️ Mua Viện Này (%s VND)" % _fmt(int(p_tile.get("base_price", 0)))
		"upgrade":
			return "📈 Nâng Điểm Viện (%s VND)" % _fmt(int(p_tile.get("upgrade_cost", 0)))
		"decline":
			return "⏭️ Bỏ Qua (Kết thúc lựa chọn)"
		"pay_rent":
			return "💰 Nộp Học Liệu (%s VND)" % _fmt(int(p_tile.get("rent", 0)))
		"teleport_unowned":
			var bp := int(tiles[tid].get("base_price", 0)) if (tid >= 0 and tid < tiles.size()) else 0
			return "🚀 Dịch chuyển + Mua %s (Phí 20k + %s)" % [t_name, _fmt(bp)]
		"teleport_owned":
			var uc := int(tiles[tid].get("upgrade_cost", 0)) if (tid >= 0 and tid < tiles.size()) else 0
			return "🏠 Dịch chuyển + Nâng cấp %s (Phí 20k + %s)" % [t_name, _fmt(uc)]
		"sell_tile":
			var sell_val := int(tiles[tid].get("sell_value", 0)) if (tid >= 0 and tid < tiles.size()) else 0
			return "🔨 Thanh lý %s (Thu về +%s VND)" % [t_name, _fmt(sell_val)]
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
