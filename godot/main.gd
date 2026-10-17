extends Control
## HUST Monopoly — 2.5D Isometric Board UI (Phase 2 Hotseat Prototype)
## Combines:
## - 2.5D Isometric diamond board with 3D extruded blocks & green central courtyard
## - Claude's Bribe fix (step = 1 on HSlider/SpinBox + max_bribe capped at 100% steal threshold)
## - Explicit GDScript types for Godot 4.4 - 4.7 compatibility and full ui_test.gd compliance

const PLAYER_COLORS: Array[Color] = [
	Color(0.92, 0.28, 0.28), # Player 1 - Red
	Color(0.22, 0.58, 0.98), # Player 2 - Blue
	Color(0.22, 0.78, 0.38), # Player 3 - Green
	Color(0.96, 0.72, 0.16), # Player 4 - Gold
]

const DICE_FACES: Array[String] = ["—", "⚀ 1", "⚁ 2", "⚂ 3", "⚃ 4", "⚄ 5", "⚅ 6"]

var client: Node
var current_snap: Dictionary = {}
var selected_tile_id: int = 0

var iso_board: IsoBoardCanvas
var status_label: Label
var player_cards: Array[RichTextLabel] = []
var player_panels: Array[PanelContainer] = []
var actions_box: VBoxContainer
var event_log: RichTextLabel

var turn_banner_label: Label
var dice_left_label: Label
var dice_right_label: Label
var dice_sum_label: Label
var latest_event_banner: RichTextLabel
var tile_inspector_label: RichTextLabel


# ============================================================================
# Custom 2.5D Isometric Board Canvas
# ============================================================================
class IsoBoardCanvas extends Control:
	signal tile_clicked(tile_id: int)

	const ISO_W: float = 38.0
	const ISO_H: float = 20.0

	var snap: Dictionary = {}
	var selected_tid: int = 0
	var tile_grid_pos: Array[Vector2i] = []
	var top_polygons: Array[PackedVector2Array] = []
	var font: Font

	func _init() -> void:
		font = ThemeDB.fallback_font
		tile_grid_pos.resize(40)
		top_polygons.resize(40)
		for r in range(11):
			for c in range(11):
				var idx: int = _rc_to_tile(r, c)
				if idx >= 0:
					tile_grid_pos[idx] = Vector2i(c, r)

	static func _rc_to_tile(r: int, c: int) -> int:
		if r == 10:
			return 10 - c             # #0 (10,10 bottom corner) -> #10 (0,10 left corner)
		if c == 0:
			return 10 + (10 - r)      # #11 -> #20 (0,0 top corner)
		if r == 0:
			return 20 + c             # #21 -> #30 (10,0 right corner)
		if c == 10:
			return 30 + r             # #31 -> #39 (10,9)
		return -1

	func _iso(gx: float, gy: float, origin: Vector2) -> Vector2:
		return origin + Vector2((gx - gy) * ISO_W, (gx + gy) * ISO_H)

	func _gui_input(event: InputEvent) -> void:
		if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
			var click_pos: Vector2 = event.position
			for i in range(39, -1, -1):
				if top_polygons[i].size() == 4 and Geometry2D.is_point_in_polygon(click_pos, top_polygons[i]):
					selected_tid = i
					tile_clicked.emit(i)
					queue_redraw()
					break

	func _draw() -> void:
		var origin: Vector2 = Vector2(size.x * 0.5, 48.0)

		# 1. Central green courtyard lawn
		var lawn_pts := PackedVector2Array([
			_iso(1.0, 1.0, origin),
			_iso(10.0, 1.0, origin),
			_iso(10.0, 10.0, origin),
			_iso(1.0, 10.0, origin),
		])
		draw_colored_polygon(lawn_pts, Color(0.56, 0.82, 0.16))
		draw_polyline(PackedVector2Array([lawn_pts[0], lawn_pts[1], lawn_pts[2], lawn_pts[3], lawn_pts[0]]), Color(0.25, 0.42, 0.08), 2.0)

		for g in range(2, 10):
			draw_line(_iso(float(g), 1.0, origin), _iso(float(g), 10.0, origin), Color(0.50, 0.76, 0.14, 0.5), 1.0)
			draw_line(_iso(1.0, float(g), origin), _iso(10.0, float(g), origin), Color(0.50, 0.76, 0.14, 0.5), 1.0)

		# 2. Painter's Algorithm back-to-front sort by (c + r)
		var draw_order: Array[int] = []
		for i in range(40):
			draw_order.append(i)
		draw_order.sort_custom(func(a: int, b: int) -> bool:
			var pa: Vector2i = tile_grid_pos[a]
			var pb: Vector2i = tile_grid_pos[b]
			if (pa.x + pa.y) != (pb.x + pb.y):
				return (pa.x + pa.y) < (pb.x + pb.y)
			return pa.x < pb.x
		)

		var tiles: Array = snap.get("tiles", [])
		var players: Array = snap.get("players", [])
		var pending_tid: int = int(snap.get("pending_tile_id", -1))

		for tid in draw_order:
			_draw_iso_tile(tid, origin, tiles, players, pending_tid)

	func _draw_iso_tile(tid: int, origin: Vector2, tiles: Array, players: Array, pending_tid: int) -> void:
		var gp: Vector2i = tile_grid_pos[tid]
		var c: float = float(gp.x)
		var r: float = float(gp.y)

		var t: Dictionary = tiles[tid] if tid < tiles.size() else {}
		var t_type: String = str(t.get("type", "institute"))
		var owner_id: int = int(t.get("owner_id", -1))
		var grade: String = str(t.get("grade", "none"))
		var is_corner: bool = (tid % 10 == 0)

		var h: float = 14.0
		if is_corner:
			h = 26.0
		elif t_type == "card":
			h = 12.0
		else:
			match grade:
				"D": h = 18.0
				"C": h = 23.0
				"B": h = 29.0
				"A": h = 36.0
				"A+": h = 44.0
				_: h = 14.0

		var t0: Vector2 = _iso(c, r, origin)
		var r0: Vector2 = _iso(c + 1.0, r, origin)
		var b0: Vector2 = _iso(c + 1.0, r + 1.0, origin)
		var l0: Vector2 = _iso(c, r + 1.0, origin)

		var up := Vector2(0.0, -h)
		var top_t: Vector2 = t0 + up
		var top_r: Vector2 = r0 + up
		var top_b: Vector2 = b0 + up
		var top_l: Vector2 = l0 + up

		var top_poly := PackedVector2Array([top_t, top_r, top_b, top_l])
		top_polygons[tid] = top_poly
		var left_wall := PackedVector2Array([top_l, top_b, b0, l0])
		var right_wall := PackedVector2Array([top_b, top_r, r0, b0])

		var top_col := Color(0.93, 0.94, 0.96)
		if owner_id >= 0 and owner_id < 4:
			top_col = PLAYER_COLORS[owner_id].lightened(0.32)
		elif is_corner:
			top_col = Color(0.98, 0.90, 0.68)
		elif t_type == "card":
			top_col = Color(0.84, 0.90, 0.98)

		if tid == pending_tid:
			top_col = top_col.lightened(0.25)

		var left_col: Color = top_col.darkened(0.22)
		var right_col: Color = top_col.darkened(0.38)
		var edge_col := Color(0.12, 0.14, 0.18)
		var edge_w: float = 1.5
		if tid == selected_tid:
			edge_col = Color(1.0, 0.95, 0.2)
			edge_w = 2.8
		elif tid == pending_tid:
			edge_col = Color(1.0, 1.0, 1.0)
			edge_w = 2.5

		draw_colored_polygon(left_wall, left_col)
		draw_colored_polygon(right_wall, right_col)
		draw_colored_polygon(top_poly, top_col)

		draw_polyline(PackedVector2Array([top_l, l0, b0, r0, top_r]), edge_col, 1.3)
		draw_line(top_b, b0, edge_col, 1.3)
		draw_polyline(PackedVector2Array([top_t, top_r, top_b, top_l, top_t]), edge_col, edge_w)

		var center: Vector2 = (top_t + top_b) * 0.5
		var label_txt: String = "#%d" % tid
		if grade != "none" and grade != "":
			label_txt += "[%s]" % grade
		elif is_corner:
			match tid:
				0: label_txt = "#0 START"
				10: label_txt = "#10 THI LẠI"
				20: label_txt = "#20 ĐÀO TẠO"
				30: label_txt = "#30 CẢNH CÁO"
		elif t_type == "card":
			label_txt = "#%d THẺ" % tid

		draw_string(font, center + Vector2(-18.0, 4.0), label_txt, HORIZONTAL_ALIGNMENT_CENTER, 40.0, 10, Color(0.08, 0.10, 0.12))

		var here_pids: Array[int] = []
		for p in players:
			var pd: Dictionary = p
			if not bool(pd.get("is_bankrupt", false)) and int(pd.get("position", -1)) == tid:
				here_pids.append(int(pd.get("id", 0)))

		var offsets: Array[Vector2] = [
			Vector2(-11.0, -8.0),
			Vector2(11.0, -8.0),
			Vector2(-11.0, 5.0),
			Vector2(11.0, 5.0),
		]
		for k in range(here_pids.size()):
			var pid: int = here_pids[k]
			var tok_pos: Vector2 = center + (offsets[k] if here_pids.size() > 1 else Vector2(0.0, -6.0))
			draw_circle(tok_pos + Vector2(0.0, 3.0), 7.5, Color(0, 0, 0, 0.45))
			draw_circle(tok_pos, 7.5, PLAYER_COLORS[clampi(pid, 0, 3)])
			draw_arc(tok_pos, 7.5, 0.0, TAU, 20, Color.WHITE, 1.8)
			draw_string(font, tok_pos + Vector2(-5.0, 4.0), str(pid + 1), HORIZONTAL_ALIGNMENT_CENTER, 12.0, 10, Color.WHITE)


# ============================================================================
# Main Controller & HUD
# ============================================================================
func _ready() -> void:
	client = preload("res://engine_client.gd").new()
	add_child(client)
	_build_ui()
	await get_tree().process_frame
	_start_new_game()


func _build_ui() -> void:
	var bg := ColorRect.new()
	bg.color = Color(0.09, 0.11, 0.15)
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)

	var root_hbox := HBoxContainer.new()
	root_hbox.set_anchors_preset(Control.PRESET_FULL_RECT)
	root_hbox.add_theme_constant_override("separation", 10)
	add_child(root_hbox)

	var left_vbox := VBoxContainer.new()
	left_vbox.custom_minimum_size = Vector2(870, 0)
	left_vbox.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	left_vbox.size_flags_vertical = Control.SIZE_EXPAND_FILL
	left_vbox.add_theme_constant_override("separation", 6)
	root_hbox.add_child(left_vbox)

	var top_stage := PanelContainer.new()
	var top_style := StyleBoxFlat.new()
	top_style.bg_color = Color(0.13, 0.16, 0.21)
	top_style.border_color = Color(0.28, 0.34, 0.44)
	top_style.set_border_width_all(1)
	top_style.set_corner_radius_all(6)
	top_style.set_content_margin_all(8)
	top_stage.add_theme_stylebox_override("panel", top_style)
	left_vbox.add_child(top_stage)

	var stage_hbox := HBoxContainer.new()
	stage_hbox.add_theme_constant_override("separation", 18)
	top_stage.add_child(stage_hbox)

	turn_banner_label = Label.new()
	turn_banner_label.add_theme_font_size_override("font_size", 18)
	turn_banner_label.text = "HUST MONOPOLY"
	turn_banner_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	stage_hbox.add_child(turn_banner_label)

	dice_left_label = _make_die_box()
	dice_right_label = _make_die_box()
	stage_hbox.add_child(dice_left_label.get_parent())
	stage_hbox.add_child(dice_right_label.get_parent())

	dice_sum_label = Label.new()
	dice_sum_label.add_theme_font_size_override("font_size", 16)
	dice_sum_label.custom_minimum_size = Vector2(140, 0)
	dice_sum_label.text = "Ready"
	stage_hbox.add_child(dice_sum_label)

	iso_board = IsoBoardCanvas.new()
	iso_board.custom_minimum_size = Vector2(860, 560)
	iso_board.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	iso_board.size_flags_vertical = Control.SIZE_EXPAND_FILL
	iso_board.tile_clicked.connect(func(tid: int) -> void:
		selected_tile_id = tid
		_update_tile_inspector()
	)
	left_vbox.add_child(iso_board)

	var bottom_hbox := HBoxContainer.new()
	bottom_hbox.custom_minimum_size = Vector2(0, 155)
	bottom_hbox.add_theme_constant_override("separation", 8)
	left_vbox.add_child(bottom_hbox)

	var insp_panel := PanelContainer.new()
	insp_panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	insp_panel.add_theme_stylebox_override("panel", top_style)
	bottom_hbox.add_child(insp_panel)

	tile_inspector_label = RichTextLabel.new()
	tile_inspector_label.bbcode_enabled = true
	insp_panel.add_child(tile_inspector_label)

	var ev_panel := PanelContainer.new()
	ev_panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	ev_panel.add_theme_stylebox_override("panel", top_style)
	bottom_hbox.add_child(ev_panel)

	latest_event_banner = RichTextLabel.new()
	latest_event_banner.bbcode_enabled = true
	ev_panel.add_child(latest_event_banner)

	# --- Right Sidebar ---
	var sidebar := VBoxContainer.new()
	sidebar.custom_minimum_size = Vector2(460, 0)
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
	new_game_btn.text = "🔄 Ván Mới"
	new_game_btn.pressed.connect(_start_new_game)
	top_bar.add_child(new_game_btn)

	var players_grid := GridContainer.new()
	players_grid.columns = 2
	players_grid.add_theme_constant_override("h_separation", 8)
	players_grid.add_theme_constant_override("v_separation", 6)
	sidebar.add_child(players_grid)

	for i in range(4):
		var card_panel := PanelContainer.new()
		card_panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		card_panel.custom_minimum_size = Vector2(220, 92)
		var rtl := RichTextLabel.new()
		rtl.bbcode_enabled = true
		rtl.fit_content = true
		rtl.scroll_active = false
		card_panel.add_child(rtl)
		players_grid.add_child(card_panel)
		player_panels.append(card_panel)
		player_cards.append(rtl)

	var actions_title := Label.new()
	actions_title.text = "Hành động hợp lệ (Server-Driven):"
	actions_title.add_theme_font_size_override("font_size", 14)
	sidebar.add_child(actions_title)

	var actions_scroll := ScrollContainer.new()
	actions_scroll.custom_minimum_size = Vector2(0, 260)
	actions_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	sidebar.add_child(actions_scroll)

	actions_box = VBoxContainer.new()
	actions_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	actions_box.add_theme_constant_override("separation", 6)
	actions_scroll.add_child(actions_box)

	var log_title := Label.new()
	log_title.text = "Lịch sử ván đấu:"
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
	panel.custom_minimum_size = Vector2(68, 44)
	var st := StyleBoxFlat.new()
	st.bg_color = Color(0.20, 0.24, 0.30)
	st.border_color = Color(0.55, 0.65, 0.78)
	st.set_border_width_all(2)
	st.set_corner_radius_all(6)
	panel.add_theme_stylebox_override("panel", st)

	var lbl := Label.new()
	lbl.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	lbl.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	lbl.add_theme_font_size_override("font_size", 20)
	lbl.text = "—"
	panel.add_child(lbl)
	return lbl


func _start_new_game() -> void:
	event_log.clear()
	_append_log("[color=yellow]=== Bắt đầu ván mới ===[/color]")
	var resp: Dictionary = await client.new_game()
	_handle_response(resp)


func _send_player_action(action: Dictionary) -> void:
	var resp: Dictionary = await client.send_action(action)
	_handle_response(resp)


func _handle_response(resp: Dictionary) -> void:
	if not resp.get("ok", false):
		var err_code: String = str(resp.get("error", "error"))
		var err_msg: String = str(resp.get("message", "Unknown error"))
		status_label.text = "Error [%s]: %s" % [err_code, err_msg]
		_append_log("[color=red]ERROR (%s): %s[/color]" % [err_code, err_msg])
		return

	var events: Array = resp.get("events", [])
	var recent_lines: PackedStringArray = []
	for ev in events:
		var line: String = str(ev)
		_append_log("• " + line)
		recent_lines.append("• " + line)

	if recent_lines.size() > 0:
		latest_event_banner.text = "[b][color=#ffd966]⚡ Diễn biến vừa xảy ra:[/color][/b]\n" + "\n".join(recent_lines)
	else:
		latest_event_banner.text = "[color=#aaaaaa]Chưa có sự kiện mới.[/color]"

	current_snap = resp.get("snapshot", {})
	var cur_pid: int = int(current_snap.get("current_player_id", 0))
	var players: Array = current_snap.get("players", [])
	if cur_pid >= 0 and cur_pid < players.size():
		selected_tile_id = int(players[cur_pid].get("position", 0))
	var pending_tid: int = int(current_snap.get("pending_tile_id", -1))
	if pending_tid >= 0:
		selected_tile_id = pending_tid

	iso_board.snap = current_snap
	iso_board.selected_tid = selected_tile_id
	iso_board.queue_redraw()

	_render_snapshot(current_snap)


func _render_snapshot(snap: Dictionary) -> void:
	if snap.is_empty():
		return

	var tempo: int = int(snap.get("tempo", 1))
	var max_tempo: int = int(snap.get("max_tempo", 48))
	var phase: String = str(snap.get("phase", ""))
	var cur_pid: int = int(snap.get("current_player_id", 0))
	var dice: Array = snap.get("last_dice", [0, 0])
	var d1: int = int(dice[0])
	var d2: int = int(dice[1])
	var game_over: bool = bool(snap.get("game_over", false))

	dice_left_label.text = DICE_FACES[clampi(d1, 0, 6)]
	dice_right_label.text = DICE_FACES[clampi(d2, 0, 6)]
	if d1 > 0 and d2 > 0:
		var dbl: String = " (ĐÔI!)" if d1 == d2 else ""
		dice_sum_label.text = "Tổng = %d%s" % [d1 + d2, dbl]
	else:
		dice_sum_label.text = "Chờ đổ xúc xắc"

	if game_over:
		var wid: int = int(snap.get("winner_id", -1))
		status_label.text = "🏆 KẾT THÚC! Người thắng: Player %d" % [wid + 1]
		turn_banner_label.text = "🏆 NGƯỜI THẮNG: PLAYER %d 🏆" % [wid + 1]
	else:
		status_label.text = "Tempo %d/%d  |  Phase: %s" % [tempo, max_tempo, phase]
		turn_banner_label.text = "▶ LƯỢT CỦA PLAYER %d  (Tempo %d/%d)" % [cur_pid + 1, tempo, max_tempo]
		turn_banner_label.modulate = PLAYER_COLORS[clampi(cur_pid, 0, 3)]

	var players: Array = snap.get("players", [])
	for i in range(min(4, players.size())):
		var p: Dictionary = players[i]
		var pid: int = int(p.get("id", i))
		var p_color: String = PLAYER_COLORS[pid].to_html(false)
		var is_turn: bool = (pid == cur_pid and not game_over)
		var turn_tag: String = " [b][color=yellow]◀ ĐANG ĐI[/color][/b]" if is_turn else ""

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

		var extra: String = (" | " + ", ".join(status_tags)) if status_tags.size() > 0 else ""
		player_cards[i].text = (
			"[color=#%s][b]%s[/b][/color]%s\n" % [p_color, str(p.get("name", "P")), turn_tag] +
			"Tiền: [b]%s[/b] | Tài sản: %s\n" % [_fmt(int(p.get("cash", 0))), _fmt(int(p.get("net_worth", 0)))] +
			"Ô: #%d | Số viện: %d%s" % [int(p.get("position", 0)), (p.get("owned_tile_ids", []) as Array).size(), extra]
		)

	var tiles: Array = snap.get("tiles", [])
	var pending_tid: int = int(snap.get("pending_tile_id", -1))
	_update_tile_inspector()
	_render_legal_actions(snap, tiles, players, cur_pid, pending_tid)


func _update_tile_inspector() -> void:
	var tiles: Array = current_snap.get("tiles", [])
	if selected_tile_id < 0 or selected_tile_id >= tiles.size():
		return
	var t: Dictionary = tiles[selected_tile_id]
	var tid: int = int(t.get("id", selected_tile_id))
	var t_name: String = str(t.get("name", ""))
	var t_type: String = str(t.get("type", ""))
	var owner_id: int = int(t.get("owner_id", -1))
	var owner_text: String = ("Player %d" % (owner_id + 1)) if owner_id >= 0 else "Chưa ai sở hữu"

	if t_type == "institute":
		tile_inspector_label.text = (
			"[b][color=#9fc5e8]🔍 Ô #%d: %s[/color][/b] [i](Click khối 3D bất kỳ để soi)[/i]\n" % [tid, t_name] +
			"• Chủ: [b]%s[/b] | Cấp điểm: [b]%s[/b] | Vốn tích lũy: [b]%s[/b]\n" % [
				owner_text, str(t.get("grade", "none")), _fmt(int(t.get("total_invested", 0)))
			] +
			"• Giá mua: [b]%s[/b] | Phí nâng điểm: [b]%s[/b]\n" % [
				_fmt(int(t.get("base_price", 0))), _fmt(int(t.get("upgrade_cost", 0)))
			] +
			"• Tiền học liệu: [b][color=#ff9999]%s[/color][/b] | Thanh lý (50%%): [b]%s[/b]" % [
				_fmt(int(t.get("rent", 0))), _fmt(int(t.get("sell_value", 0)))
			]
		)
	else:
		tile_inspector_label.text = (
			"[b][color=#9fc5e8]🔍 Ô #%d: %s[/color][/b]\n" % [tid, t_name] +
			"• Loại ô đặc biệt: [b]%s[/b]" % t_type
		)


func _render_legal_actions(snap: Dictionary, tiles: Array, players: Array, cur_pid: int, pending_tid: int) -> void:
	for child in actions_box.get_children():
		child.queue_free()

	var legal: Dictionary = snap.get("legal", {})
	var acts: Array = legal.get("actions", [])
	var min_bribe: int = int(legal.get("min_bribe", 1))
	var max_bribe: int = int(legal.get("max_bribe", 1))

	for a in acts:
		var act: Dictionary = a
		var a_type: String = str(act.get("type", ""))
		var tid: int = int(act.get("tile_id", -1))

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
	var total_inv: int = 1
	if pending_tid >= 0 and pending_tid < tiles.size():
		total_inv = maxi(1, int(tiles[pending_tid].get("total_invested", 1)))
	var bonus_pct: int = 0
	if cur_pid >= 0 and cur_pid < players.size():
		bonus_pct = int(players[cur_pid].get("steal_bonus_percent", 0))

	# Cap max_bribe at the exact amount needed for 100% steal chance so players never waste money
	var needed_for_100: int = int(ceil(float(total_inv) * float(maxi(0, 100 - bonus_pct)) / 100.0))
	max_bribe = maxi(min_bribe, mini(max_bribe, needed_for_100))

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

	var default_bribe: int = clampi(total_inv / 2, min_bribe, max_bribe)

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
	spin.step = 1  # step = 1 prevents snapping to 5001, 10001...
	spin.value = default_bribe
	spin.custom_minimum_size = Vector2(120, 0)
	hbox_input.add_child(spin)

	var quick_hbox := HBoxContainer.new()
	quick_hbox.add_theme_constant_override("separation", 6)
	vbox.add_child(quick_hbox)

	var steal_btn := Button.new()
	steal_btn.custom_minimum_size = Vector2(0, 40)

	var update_steal_ui: Callable = func(bribe_val: int) -> void:
		var raw_pct: float = (float(bribe_val) / float(total_inv)) * 100.0 + float(bonus_pct)
		var win_pct: float = clampf(raw_pct, 0.0, 100.0)
		var bonus_note: String = (" (đã cộng +%d%% Phao thi)" % bonus_pct) if bonus_pct > 0 else ""
		info_lbl.text = "🕵️ Cược Thi Hộ (Vốn ô đất: %s VND)%s" % [_fmt(total_inv), bonus_note]
		steal_btn.text = "🔥 Cược %s VND  ➔  Tỷ lệ cướp thành công: %.1f%%" % [_fmt(bribe_val), win_pct]

	for target_pct in [25, 50, 75, 100]:
		var qbtn := Button.new()
		qbtn.text = "%d%%" % target_pct
		qbtn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		qbtn.pressed.connect(func() -> void:
			var needed_pct: int = maxi(0, target_pct - bonus_pct)
			var target_bribe: int = int(ceil(float(total_inv) * float(needed_pct) / 100.0))
			target_bribe = clampi(target_bribe, min_bribe, max_bribe)
			slider.value = target_bribe
			spin.value = target_bribe
			update_steal_ui.call(target_bribe)
		)
		quick_hbox.add_child(qbtn)

	slider.value_changed.connect(func(v: float) -> void:
		var iv: int = clampi(int(v), min_bribe, max_bribe)
		if int(spin.value) != iv:
			spin.set_value_no_signal(iv)
		update_steal_ui.call(iv)
	)
	spin.value_changed.connect(func(v: float) -> void:
		var iv: int = clampi(int(v), min_bribe, max_bribe)
		if int(slider.value) != iv:
			slider.set_value_no_signal(iv)
		update_steal_ui.call(iv)
	)

	update_steal_ui.call(default_bribe)

	steal_btn.pressed.connect(func() -> void:
		var payload: Dictionary = act.duplicate()
		payload["amount"] = clampi(int(spin.value), min_bribe, max_bribe)
		_send_player_action(payload)
	)
	vbox.add_child(steal_btn)


func _format_action_button(a_type: String, tid: int, tiles: Array, pending_tid: int) -> String:
	var t_name: String = ("#%d %s" % [tid, str(tiles[tid].get("name", ""))]) if (tid >= 0 and tid < tiles.size()) else ""
	var p_tile: Dictionary = tiles[pending_tid] if (pending_tid >= 0 and pending_tid < tiles.size()) else {}
	match a_type:
		"roll_dice":
			return "🎲 Đổ Xúc Xắc"
		"pay_retake_fee":
			return "💸 Nộp 50,000 VND Phí Thi Lại (Ra phòng thi lại)"
		"buy":
			return "🏛️ Mua Viện Này (%s VND)" % _fmt(int(p_tile.get("base_price", 0)))
		"upgrade":
			return "📈 Nâng Điểm Viện (%s VND)" % _fmt(int(p_tile.get("upgrade_cost", 0)))
		"decline":
			return "⏭️ Bỏ Qua (Kết thúc lựa chọn)"
		"pay_rent":
			return "💰 Nộp Học Liệu (%s VND)" % _fmt(int(p_tile.get("rent", 0)))
		"teleport_unowned":
			var bp: int = int(tiles[tid].get("base_price", 0)) if (tid >= 0 and tid < tiles.size()) else 0
			return "🚀 Dịch chuyển + Mua %s (Phí 20k + %s)" % [t_name, _fmt(bp)]
		"teleport_owned":
			var gr: String = str(tiles[tid].get("grade", "")) if (tid >= 0 and tid < tiles.size()) else ""
			if gr == "A+":
				return "🏠 Dịch chuyển về %s (Đã đạt A+, chỉ tốn phí dịch chuyển)" % t_name
			var uc: int = int(tiles[tid].get("upgrade_cost", 0)) if (tid >= 0 and tid < tiles.size()) else 0
			return "🏠 Dịch chuyển + Nâng cấp %s (+%s VND)" % [t_name, _fmt(uc)]
		"sell_tile":
			var sell_val: int = int(tiles[tid].get("sell_value", 0)) if (tid >= 0 and tid < tiles.size()) else 0
			return "🔨 Thanh lý %s (Thu về +%s VND)" % [t_name, _fmt(sell_val)]
		_:
			return a_type + (" " + t_name if not t_name.is_empty() else "")


func _fmt(n: int) -> String:
	var s: String = str(abs(n))
	var out: String = ""
	for i in range(s.length()):
		if i > 0 and (s.length() - i) % 3 == 0:
			out += ","
		out += s[i]
	return ("-" + out) if n < 0 else out


func _append_log(line: String) -> void:
	event_log.append_text(line + "\n")
