extends Control

var _aja
var _input: AJAInput
var _output: AJAOutput

var _preview_texture: ImageTexture
var _output_viewport: SubViewport
var _output_viewport_texture: Texture2D
var _output_primitive: Node3D

var _device_select: OptionButton
var _input_channel_select: OptionButton
var _output_channel_select: OptionButton
var _output_format_select: OptionButton
var _refresh_button: Button
var _input_button: Button
var _output_button: Button
var _status_label: Label
var _preview: TextureRect
var _device_info: TextEdit

const OUTPUT_ROTATION_SPEED := Vector3(0.45, 0.8, 0.2)

func _ready() -> void:
	_input = get_node_or_null("AJAInput")
	_output = get_node_or_null("AJAOutput")
	_output_viewport = get_node_or_null("OutputViewport")
	_output_primitive = get_node_or_null("OutputViewport/OutputScene/Primitive")
	if _output_viewport != null:
		_output_viewport_texture = _output_viewport.get_texture()
	_build_ui()
	if Engine.has_singleton("AJAVideoSystems"):
		_aja = Engine.get_singleton("AJAVideoSystems")
	_refresh_devices()

func _exit_tree() -> void:
	_stop_input()
	_stop_output()

func _process(delta: float) -> void:
	if _output_primitive != null:
		_output_primitive.rotation += OUTPUT_ROTATION_SPEED * delta

func _build_ui() -> void:
	var root := VBoxContainer.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.offset_left = 16
	root.offset_top = 16
	root.offset_right = -16
	root.offset_bottom = -16
	root.add_theme_constant_override("separation", 10)
	add_child(root)

	var top_row := HBoxContainer.new()
	top_row.add_theme_constant_override("separation", 8)
	root.add_child(top_row)

	_device_select = OptionButton.new()
	_device_select.custom_minimum_size = Vector2(320, 0)
	_device_select.item_selected.connect(_on_device_selected)
	top_row.add_child(_device_select)

	_refresh_button = Button.new()
	_refresh_button.text = "Refresh"
	_refresh_button.pressed.connect(_refresh_devices)
	top_row.add_child(_refresh_button)

	var channel_row := HBoxContainer.new()
	channel_row.add_theme_constant_override("separation", 8)
	root.add_child(channel_row)

	var in_lbl := Label.new()
	in_lbl.text = "In Ch:"
	channel_row.add_child(in_lbl)
	_input_channel_select = OptionButton.new()
	_input_channel_select.custom_minimum_size = Vector2(120, 0)
	channel_row.add_child(_input_channel_select)

	var out_lbl := Label.new()
	out_lbl.text = "Out Ch:"
	channel_row.add_child(out_lbl)
	_output_channel_select = OptionButton.new()
	_output_channel_select.custom_minimum_size = Vector2(120, 0)
	channel_row.add_child(_output_channel_select)

	var fmt_lbl := Label.new()
	fmt_lbl.text = "Format:"
	channel_row.add_child(fmt_lbl)
	_output_format_select = OptionButton.new()
	_output_format_select.custom_minimum_size = Vector2(240, 0)
	channel_row.add_child(_output_format_select)

	var action_row := HBoxContainer.new()
	action_row.add_theme_constant_override("separation", 8)
	root.add_child(action_row)

	_input_button = Button.new()
	_input_button.text = "Start Input"
	_input_button.pressed.connect(_toggle_input)
	action_row.add_child(_input_button)

	_output_button = Button.new()
	_output_button.text = "Start Output Scene"
	_output_button.pressed.connect(_toggle_output)
	action_row.add_child(_output_button)

	_status_label = Label.new()
	_status_label.text = "Ready"
	root.add_child(_status_label)

	var body := HSplitContainer.new()
	body.size_flags_vertical = Control.SIZE_EXPAND_FILL
	root.add_child(body)

	_preview = TextureRect.new()
	_preview.expand_mode = TextureRect.EXPAND_FIT_WIDTH_PROPORTIONAL
	_preview.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	_preview.custom_minimum_size = Vector2(720, 405)
	body.add_child(_preview)

	_device_info = TextEdit.new()
	_device_info.editable = false
	_device_info.wrap_mode = TextEdit.LINE_WRAPPING_BOUNDARY
	_device_info.custom_minimum_size = Vector2(360, 0)
	body.add_child(_device_info)

func _refresh_devices() -> void:
	_stop_input()
	_stop_output()
	_preview.texture = null
	_preview_texture = null
	if _input != null:
		_input.set_texture(null)
	if _output != null:
		_output.set_texture(null)

	_device_select.clear()
	_device_info.text = ""

	if _aja == null:
		_set_controls_enabled(false)
		_status_label.text = "AJA extension is not loaded"
		return

	_aja.refresh()
	var devices: Array = _aja.get_devices()

	for device in devices:
		var index: int = int(device.get("index", 0))
		var label := "%d: %s" % [index, _device_label(device)]
		_device_select.add_item(label)
		_device_select.set_item_metadata(_device_select.item_count - 1, index)

	_set_controls_enabled(not devices.is_empty())

	if devices.is_empty():
		_status_label.text = "No AJA devices found"
		_input_channel_select.clear()
		_output_channel_select.clear()
		_output_format_select.clear()
		return

	_device_select.select(0)
	_load_info_for_selected_device()
	_status_label.text = "Found %d AJA device(s)" % devices.size()

func _on_device_selected(_index: int) -> void:
	_stop_input()
	_stop_output()
	_load_info_for_selected_device()

func _load_info_for_selected_device() -> void:
	var device_index := _selected_device_index()
	if device_index < 0:
		return

	var device = _aja.get_device(device_index)
	if device == null:
		return

	_fill_channel_select(_input_channel_select, device.get_num_video_inputs())
	_fill_channel_select(_output_channel_select, device.get_num_video_outputs())
	_fill_format_select(_output_format_select, device.get_video_formats())

	var lines: Array[String] = []
	lines.append("Device: %s" % device.get_display_name())
	lines.append("Model: %s" % device.get_model_name())
	lines.append("Video inputs: %d" % device.get_num_video_inputs())
	lines.append("Video outputs: %d" % device.get_num_video_outputs())
	lines.append("Can capture: %s" % str(device.can_capture()))
	lines.append("Can playback: %s" % str(device.can_playback()))
	lines.append("Video formats: %d" % _output_format_select.item_count)
	_device_info.text = "\n".join(lines)

func _fill_channel_select(select: OptionButton, count: int) -> void:
	select.clear()
	for i in count:
		select.add_item("Ch %d" % (i + 1))
		select.set_item_metadata(select.item_count - 1, i)
	select.disabled = (count == 0)
	if count > 0:
		select.select(0)

func _fill_format_select(select: OptionButton, formats: Array) -> void:
	select.clear()
	for fmt in formats:
		var name: String = str(fmt.get("name", ""))
		var w: int = int(fmt.get("width", 0))
		var h: int = int(fmt.get("height", 0))
		var prog: bool = bool(fmt.get("progressive", true))
		var label := "%s  %dx%d  %s" % [name, w, h, "p" if prog else "i"]
		select.add_item(label)
		select.set_item_metadata(select.item_count - 1, int(fmt.get("id", 0)))
	select.disabled = formats.is_empty()
	if not formats.is_empty():
		select.select(0)

func _toggle_input() -> void:
	if _input != null and _input.is_enabled():
		_stop_input()
		return
	if _output != null and _output.is_enabled():
		_stop_output()

	var device_index := _selected_device_index()
	var channel := _selected_channel(_input_channel_select)
	if device_index < 0:
		_status_label.text = "Select an input device"
		return

	if _input == null:
		_status_label.text = "AJAInput class is not available"
		return

	_preview_texture = ImageTexture.new()
	_preview.texture = _preview_texture
	_input.set_texture(_preview_texture)
	_input.device = device_index
	_input.channel = channel
	_input.enabled = true

	if _input.is_open():
		_input_button.text = "Stop Input"
		_status_label.text = "Input started on Ch %d (%dx%d)" % [
			channel + 1, _input.get_width(), _input.get_height()]
	else:
		_preview.texture = null
		_preview_texture = null
		_input.set_texture(null)
		_status_label.text = "Input failed to open"

func _toggle_output() -> void:
	if _output != null and _output.is_enabled():
		_stop_output()
		return
	if _input != null and _input.is_enabled():
		_stop_input()

	var device_index := _selected_device_index()
	var channel := _selected_channel(_output_channel_select)
	var fmt := _selected_format(_output_format_select)
	if device_index < 0 or fmt == 0:
		_status_label.text = "Select an output device and format"
		return

	if _output == null:
		_status_label.text = "AJAOutput class is not available"
		return

	_output.device = device_index
	_output.channel = channel
	_output.video_format = fmt
	_output.enabled = true

	if _output.is_open():
		var output_size := Vector2i(_output.get_width(), _output.get_height())
		if _output_viewport != null and output_size.x > 0 and output_size.y > 0:
			_output_viewport.size = output_size
			_output_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
			_output_viewport_texture = _output_viewport.get_texture()
		_output.set_texture(_output_viewport_texture)
		_preview.texture = _output_viewport_texture
		_output_button.text = "Stop Output Scene"
		_status_label.text = "Output scene started on Ch %d (%dx%d)" % [
			channel + 1, _output.get_width(), _output.get_height()]
	else:
		_status_label.text = "Output failed to open"

func _stop_input() -> void:
	if _input != null:
		_input.enabled = false
		_input.set_texture(null)
	if _input_button != null:
		_input_button.text = "Start Input"

func _stop_output() -> void:
	if _output != null:
		_output.enabled = false
		_output.set_texture(null)
	if _output_button != null:
		_output_button.text = "Start Output Scene"

func _set_controls_enabled(enabled: bool) -> void:
	_device_select.disabled = not enabled
	_input_button.disabled = not enabled
	_output_button.disabled = not enabled

func _selected_device_index() -> int:
	var sel := _device_select.selected
	if sel < 0:
		return -1
	return int(_device_select.get_item_metadata(sel))

func _selected_channel(select: OptionButton) -> int:
	var sel := select.selected
	if sel < 0:
		return 0
	return int(select.get_item_metadata(sel))

func _selected_format(select: OptionButton) -> int:
	var sel := select.selected
	if sel < 0:
		return 0
	return int(select.get_item_metadata(sel))

func _device_label(device: Dictionary) -> String:
	var n := str(device.get("display_name", ""))
	return n if not n.is_empty() else str(device.get("model_name", "AJA Device"))
