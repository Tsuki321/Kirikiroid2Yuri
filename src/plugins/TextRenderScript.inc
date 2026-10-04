TJS_W(R"K2TEXT(
// Portable TextRenderBase. Rasterization stays with the framework's Font/Layer
// callbacks; this class supplies layout, formatting, ruby, links and timing.
class TextRenderBase {
    var defaultFace = "user", defaultFontSize = 24;
    var defaultBigFontSize = 150, defaultSmallFontSize = 75;
    var defaultLineSize = 0, defaultLineSpacing = 6, defaultPitch = 0;
    var defaultAlign = -1, defaultValign = -1, defaultRubySize = 10, defaultRubyOffset = -2;
    var defaultChColor = 0xffffff, defaultShadow = false, defaultShadowColor = 0;
    var defaultShadowDiff = 0, defaultEdge = false, defaultEdgeColor = 0;
    var defaultBold = false, defaultItalic = false, timeScale = 1, fontScale = 1;
    var vertical = false, renderOver = false;
    var renderLeft = 0, renderTop = 0, renderRight = 0, renderBottom = 0;
    var _chars, _lines, _keys, _rubies, _options, _state;
    var _width = 0, _height = 0, _pen = 0, _block = 0, _lineHeight = 0, _lineStart = 0;
    var _indent = 0, _autoIndent = 0, _clock = 0, _speed = 1, _absoluteSpeed = false;
    var _link = "", _plain = "", _source = "", _dirty = true, _all = false;

    function TextRenderBase() {
        _options = %["following" => "、。，．！？!?)]}」』】）］｝ーぁぃぅぇぉっゃゅょ",
                     "leading" => "([{「『【（［｛", "begin" => "「『（", "end" => "」』）",
                     "wordbreak" => true, "kinsoku_max" => 1, "ignore_delay" => false,
                     "width_time_scale" => false, "ignore_xr" => false];
        clear();
    }
    function _copy(value) {
        var copy = %[];
        if (value !== void) (Dictionary.assign incontextof copy)(value);
        return copy;
    }
    function _finite(value, limit = 100000000) {
        value = +value;
        if (value != value || value > limit || value < -limit)
            throw "TextRenderBase: invalid numeric value";
        return value;
    }
    function _font() {
        return %["face" => defaultFace, "size" => defaultFontSize,
            "bold" => defaultBold, "italic" => defaultItalic, "color" => defaultChColor,
            "rubySize" => defaultRubySize, "rubyOffset" => defaultRubyOffset,
            "shadow" => defaultShadow, "shadowColor" => defaultShadowColor,
            "shadowDiff" => defaultShadowDiff, "edge" => defaultEdge, "edgeColor" => defaultEdgeColor,
            "pitch" => defaultPitch, "lineSpacing" => defaultLineSpacing,
            "lineSize" => defaultLineSize, "align" => defaultAlign, "valign" => defaultValign];
    }
    function _merge(target, value, names) {
        if (value === void) return;
        for (var i = 0; i < names.count; ++i) {
            var key = names[i];
            if (value[key] !== void) target[key] = value[key];
        }
    }
    function setDefault(value) {
        if (value === void) return;
        var keys = ["face", "size", "fontSize", "fontsize", "bigfontsize", "smallfontsize",
                    "lineSize", "lineSpacing", "pitch", "align", "valign", "rubySize", "rubyOffset",
                    "color", "shadow", "shadowColor", "shadowDiff", "edge", "edgeColor", "bold", "italic", "chColor"];
        var members = ["defaultFace", "defaultFontSize", "defaultFontSize", "defaultFontSize",
                    "defaultBigFontSize", "defaultSmallFontSize", "defaultLineSize", "defaultLineSpacing",
                    "defaultPitch", "defaultAlign", "defaultValign", "defaultRubySize", "defaultRubyOffset",
                    "defaultChColor", "defaultShadow", "defaultShadowColor", "defaultShadowDiff",
                    "defaultEdge", "defaultEdgeColor", "defaultBold", "defaultItalic", "defaultChColor"];
        for (var i = 0; i < keys.count; ++i)
            if (value[keys[i]] !== void) this[members[i]] = value[keys[i]];
        _state = _font();
    }
    function setOption(value) {
        _merge(_options, value, ["following", "leading", "begin", "end", "wordbreak", "kinsoku_max",
                                "ignore_delay", "width_time_scale", "ignore_xr"]);
        if (value !== void && value.word_break !== void) _options.wordbreak = !!value.word_break;
        if (value !== void && value.vertical !== void) vertical = !!value.vertical;
        _dirty = true;
    }
    function setFont(value) {
        _merge(_state, value, ["face", "size", "bold", "italic", "color", "rubySize", "rubyOffset",
                              "shadow", "shadowColor", "shadowDiff", "edge", "edgeColor"]);
        if (value !== void) {
            if (value.chColor !== void) _state.color = value.chColor;
            if (value.fontSize !== void) _state.size = value.fontSize;
        }
    }
    function setStyle(value) {
        _merge(_state, value, ["pitch", "lineSpacing", "lineSize", "align", "valign"]);
    }
    function resetFont() {
        var defaults = _font();
        (global.TextRenderBase.setFont incontextof this)(defaults);
    }
    function resetStyle() { setStyle(_font()); }
    property bold { getter() { return _state.bold; } setter(value) { _state.bold = !!value; } }
    property italic { getter() { return _state.italic; } setter(value) { _state.italic = !!value; } }
    property face { getter() { return _state.face; } setter(value) { _state.face = "" + value; } }
    property fontSize { getter() { return _state.size; } setter(value) { _state.size = _finite(value, 4096); } }
    property chColor { getter() { return _state.color; } setter(value) { _state.color = int value; } }
    property rubySize { getter() { return _state.rubySize; } setter(value) { _state.rubySize = _finite(value, 4096); } }
    property rubyOffset { getter() { return _state.rubyOffset; } setter(value) { _state.rubyOffset = _finite(value); } }
    property shadow { getter() { return _state.shadow; } setter(value) { _state.shadow = !!value; } }
    property shadowColor { getter() { return _state.shadowColor; } setter(value) { _state.shadowColor = int value; } }
    property edge { getter() { return _state.edge; } setter(value) { _state.edge = !!value; } }
    property edgeColor { getter() { return _state.edgeColor; } setter(value) { _state.edgeColor = int value; } }
    property lineSpacing { getter() { return _state.lineSpacing; } setter(value) { _state.lineSpacing = _finite(value); } }
    property pitch { getter() { return _state.pitch; } setter(value) { _state.pitch = _finite(value); } }
    property lineSize { getter() { return _state.lineSize; } setter(value) { _state.lineSize = _finite(value); } }
    function setRenderSize(width, height = 0) {
        _width = Math.max(0, _finite(width)); _height = Math.max(0, _finite(height)); _dirty = true;
    }
    function clear() {
        _chars = []; _lines = []; _keys = []; _rubies = []; _state = _font();
        _pen = _block = _lineHeight = _lineStart = _indent = _clock = 0;
        _speed = 1; _absoluteSpeed = false; _link = _plain = _source = "";
        renderLeft = renderTop = renderRight = renderBottom = 0; renderOver = false; _dirty = true;
    }
    function onFontChange(info) {}
    function onGetTextWidth(text, size) { return text.length * size * 0.5; }
    function onGetTextHeight(text, size) { return size; }
    function onGetGraphSize(name) { return %["width" => 0, "height" => 0]; }
    function onEval(expression) { return ""; }
    function onLabel(name) { return 0; }
    function _size() {
        return Math.max(1, Math.min(4096, _finite(_state.size, 4096) * _finite(fontScale, 100)));
    }
    function _measure(text, size, style = void) {
        onFontChange(style === void ? _state : style);
        var width = onGetTextWidth(text, size);
        return Math.max(0, _finite(width === void ? text.length * size * 0.5 : width));
    }
    function _codeAt(text, index) { return #text.charAt(index); }
    function _cluster(text, start) {
        var end = start + 1, c = _codeAt(text, start);
        if (c >= 0xd800 && c <= 0xdbff && end < text.length &&
            _codeAt(text, end) >= 0xdc00 && _codeAt(text, end) <= 0xdfff) ++end;
        while (end < text.length) {
            c = _codeAt(text, end);
            if ((c >= 0x300 && c <= 0x36f) || (c >= 0xfe00 && c <= 0xfe0f) ||
                (c >= 0x1ab0 && c <= 0x1aff) || (c >= 0x1dc0 && c <= 0x1dff)) ++end;
            else if (c == 0x200d && end + 1 < text.length) {
                end += 2;
                c = _codeAt(text, end - 1);
                if (c >= 0xd800 && c <= 0xdbff && end < text.length &&
                    _codeAt(text, end) >= 0xdc00 && _codeAt(text, end) <= 0xdfff) ++end;
            } else break;
        }
        return text.substr(start, end - start);
    }
    function _break(explicit = false, extra = 0) {
        var height = Math.max(_lineHeight, Math.max(_size(), +_state.lineSize));
        _lines.add(%["start" => _lineStart, "end" => _chars.count, "width" => _pen,
                     "height" => height, "block" => _block, "align" => _state.align]);
        _block += Math.max(1, height + _finite(_state.lineSpacing) + extra);
        _pen = _indent; _lineStart = _chars.count; _lineHeight = 0; _dirty = true;
        if (explicit) _plain += "\n";
    }
    function newline(extra = 0) { _break(true, _finite(extra)); }
    function _emit(text, graph = false, next = "") {
        if (_chars.count >= 200000) throw "TextRenderBase: character limit exceeded";
        var size = _size(), width, height = size;
        if (graph) {
            var dimensions = onGetGraphSize(text);
            width = Math.max(0, _finite(dimensions.width)); height = Math.max(0, _finite(dimensions.height));
        } else {
            width = _measure(text, size);
            var measured = onGetTextHeight(text, size);
            if (measured !== void) height = Math.max(size, _finite(measured));
        }
        var advance = vertical ? (graph ? height : size) : width;
        var limit = vertical ? _height : _width;
        var hanging = !graph && _options.following.indexOf(text) >= 0;
        var reserve = !graph && next != "" && _options.leading.indexOf(text) >= 0 ? _measure(next, size) : 0;
        var allowance = hanging ? Math.max(0, +_options.kinsoku_max) * size : 0;
        if (limit > 0 && _pen > _indent && _pen + advance + reserve > limit + allowance) _break();
        var ch = _copy(_state);
        ch.size = size; ch.text = text; ch.graph = graph; ch.vertical = vertical;
        // Character records expose optional effect colors, not the style flags.
        ch.edge = _state.edge ? _state.edgeColor : void;
        ch.shadow = _state.shadow ? _state.shadowColor : void;
        ch.cw = width; ch.width = vertical && !graph ? size : width; ch.height = height;
        ch.line = _lines.count; ch.inline = _pen; ch.block = _block;
        ch.link = _link == "" ? void : _link; ch.linkName = ch.link; ch.ruby = void;
        ch.duration = _absoluteSpeed ? _speed / Math.max(0.000001, +timeScale) : _speed;
        if (_options.width_time_scale && !_absoluteSpeed) ch.duration *= advance / size;
        _clock += Math.max(0, _finite(ch.duration));
        ch.delay = _clock;
        _chars.add(ch);
        _pen += Math.max(0, advance + _finite(_state.pitch));
        _lineHeight = Math.max(_lineHeight, Math.max(vertical ? width : height, +_state.lineSize));
        if (_autoIndent && _chars.count == 1 && _options.begin.indexOf(text) >= 0) _indent = _pen;
        if (!graph) _plain += text;
        _dirty = true;
    }
    function _argument(text, at, separator = ";") {
        var end = text.indexOf(separator, at);
        if (end < 0) throw "TextRenderBase: unterminated text control";
        return %["text" => text.substr(at, end - at), "end" => end + separator.length];
    }
    function _number(text, fallback = 0) {
        if (text == "") return fallback;
        if (text[0] == "$") {
            var expr = text.substr(1);
            if (expr[0] == "{" && expr[expr.length - 1] == "}") expr = expr.substr(1, expr.length - 2);
            return _finite(onEval(expr));
        }
        return _finite(+text);
    }
    function _parse(text, depth = 0) {
        if (depth > 16 || text.length > 1000000) throw "TextRenderBase: text expansion limit exceeded";
        for (var i = 0; i < text.length;) {
            var c = text[i++];
            if (c == "\r") { if (text[i] == "\n") ++i; _break(true); continue; }
            if (c == "\n") { _break(true); continue; }
            if (c == "\\") {
                if (i == text.length) { _emit("\\"); continue; }
                c = text[i++];
                if (c == "n") _break(true);
                else if (c == "i") _indent = _pen;
                else if (c == "r") { if (!_options.ignore_xr) _indent = 0; }
                else if (c == "x") {}
                else if (c == "k") _keys.add(%["pos" => _chars.count, "time" => _clock]);
                else if (c == "x") {}
                else if (c == "t") _pen = (int (_pen / (_size() * 4)) + 1) * _size() * 4;
                else if (c == "w") _pen += _measure(" ", _size());
                else _emit(c);
                continue;
            }
            if (c == "%") {
                if (i == text.length) { _emit("%"); continue; }
                c = text[i++];
                if (c == "%") { _emit("%"); continue; }
                if (c == "@") { _indent = _pen; continue; }
                if (c == "r") { _state = _font(); continue; }
                if (c == "B" || c == "S") {
                    _state.size = defaultFontSize * (c == "B" ? defaultBigFontSize : defaultSmallFontSize) / 100;
                    continue;
                }
                if (c == "L" || c == "C" || c == "R") { _state.align = c == "L" ? -1 : c == "C" ? 0 : 1; continue; }
                if (c == "b" || c == "i" || c == "s" || c == "e") {
                    var key = c == "b" ? "bold" : c == "i" ? "italic" : c == "s" ? "shadow" : "edge";
                    var flag = text[i++];
                    _state[key] = flag == "0" ? false : flag == "1" ? true : _font()[key];
                    if (text[i] == ";") ++i;
                    continue;
                }
                if ((c >= "0" && c <= "9") || c == "-" || c == "+") {
                    var value = _argument(text, i - 1); i = value.end;
                    _state.size = defaultFontSize * _number(value.text, 100) / 100; continue;
                }
                if (c == "f" || c == "p" || c == "l" || c == "n" || c == "d" || c == "a" ||
                    c == "w" || c == "t" || c == "D") {
                    var value = _argument(text, i); i = value.end;
                    if (_options.ignore_delay && "dawtD".indexOf(c) >= 0) continue;
                    if (c == "f") {
                        _state.face = value.text == "" ? defaultFace : value.text;
                        if (_state.face.substr(0, 2) == "${" && _state.face[_state.face.length - 1] == "}")
                            _state.face = "" + onEval(_state.face.substr(2, _state.face.length - 3));
                    } else if (c == "p") _state.pitch = _number(value.text, defaultPitch);
                    else if (c == "l") _link = value.text;
                    else if (c == "n") _break(true, _number(value.text));
                    else if (c == "d" || c == "a") {
                        _speed = Math.max(0, _number(value.text, c == "d" ? 100 : 1) / (c == "d" ? 100 : 1));
                        _absoluteSpeed = c == "a";
                    } else if (c == "w") _clock += Math.max(0, _number(value.text) / 100);
                    else if (c == "t") _clock += Math.max(0, _number(value.text) / Math.max(0.000001, +timeScale));
                    else {
                        var until = value.text[0] == "$" ? onLabel(value.text.substr(1)) : _number(value.text);
                        if (until !== void) _clock = Math.max(_clock, _finite(until) / Math.max(0.000001, +timeScale));
                    }
                    continue;
                }
                throw "TextRenderBase: unsupported text control %" + c;
            }
            if (c == "#") {
                var value = _argument(text, i); i = value.end;
                if (value.text == "") _state.color = defaultChColor;
                else {
                    if (value.text.length != 6) throw "TextRenderBase: color must contain six hexadecimal digits";
                    var color = 0;
                    for (var j = 0; j < 6; ++j) {
                        var digit = "0123456789abcdef".indexOf(value.text[j].toLowerCase());
                        if (digit < 0) throw "TextRenderBase: invalid hexadecimal color";
                        color = color * 16 + digit;
                    }
                    _state.color = color;
                }
                continue;
            }
            if (c == "[") {
                var value = _argument(text, i, "]"); i = value.end;
                var comma = -1, count = 1, ruby = value.text;
                for (var at = value.text.indexOf(","); at >= 0; at = value.text.indexOf(",", at + 1)) comma = at;
                if (comma >= 0) { count = int _number(value.text.substr(comma + 1), 1); ruby = value.text.substr(0, comma); }
                if (count < 1 || count > 10000) throw "TextRenderBase: invalid ruby character count";
                _rubies.add(%["start" => _chars.count, "count" => count, "text" => ruby,
                              "size" => Math.max(1, _finite(_state.rubySize, 4096) * fontScale),
                              "offset" => _finite(_state.rubyOffset), "style" => _copy(_state)]);
                continue;
            }
            if (c == "$" || c == "&") {
                var value;
                if (c == "$" && text[i] == "{") {
                    value = _argument(text, i + 1, "}"); i = value.end; if (text[i] == ";") ++i;
                } else { value = _argument(text, i); i = value.end; }
                if (c == "&") _emit(value.text, true);
                else { var expanded = onEval(value.text); if (expanded !== void) _parse("" + expanded, depth + 1); }
                continue;
            }
            var cluster = _cluster(text, i - 1);
            i += cluster.length - 1;
            // Keep a word together when it fits on an otherwise empty line.
            if (_options.wordbreak && !vertical && _width > 0 && _pen > _indent &&
                ((c >= "a" && c <= "z") || (c >= "A" && c <= "Z")) &&
                (i - cluster.length == 0 || text[i - cluster.length - 1] == " ")) {
                var end = i;
                while (end < text.length && ((text[end] >= "a" && text[end] <= "z") ||
                    (text[end] >= "A" && text[end] <= "Z") || text[end] == "'")) ++end;
                var wordWidth = _measure(text.substr(i - cluster.length, end - i + cluster.length), _size());
                if (wordWidth <= _width - _indent && _pen + wordWidth > _width) _break();
            }
            _emit(cluster, false, i < text.length ? _cluster(text, i) : "");
        }
    }
    function render(text, autoIndent = 0, diff = 0, all = 0, same = 0, reserved = 0) {
        text = "" + text; _autoIndent = autoIndent; _all = !!all;
        if (diff && text.substr(0, _source.length) == _source) {
            var addition = text.substr(_source.length); _source = text; _parse(addition);
        } else { _source += text; _parse(text); }
        done();
        return !renderOver;
    }
    function done() {
        if (!_dirty) return;
        var lines = []; (Array.assign incontextof lines)(_lines);
        if (_lineStart < _chars.count || lines.count == 0)
            lines.add(%["start" => _lineStart, "end" => _chars.count, "width" => _pen,
                "height" => Math.max(_lineHeight, _size()), "block" => _block, "align" => _state.align]);
        renderLeft = renderTop = renderRight = renderBottom = 0;
        var blockLimit = vertical ? _width : _height;
        var extent = _chars.count == 0 ? 0 : _block + Math.max(_lineHeight, _size());
        var blockShift = blockLimit > extent && _state.valign >= 0 ?
                         (blockLimit - extent) * (_state.valign == 0 ? 0.5 : 1) : 0;
        for (var n = 0; n < lines.count; ++n) {
            var line = lines[n], limit = vertical ? _height : _width;
            var shift = limit > line.width && line.align >= 0 ?
                        (limit - line.width) * (line.align == 0 ? 0.5 : 1) : 0;
            for (var j = line.start; j < line.end; ++j) {
                var ch = _chars[j];
                ch.x = vertical ? _width - ch.block - ch.width - blockShift : ch.inline + shift;
                ch.y = vertical ? ch.inline + shift : ch.block + blockShift;
                ch.ruby = void;
                if (j == 0) { renderLeft = ch.x; renderTop = ch.y; }
                renderLeft = Math.min(renderLeft, ch.x); renderTop = Math.min(renderTop, ch.y);
                renderRight = Math.max(renderRight, ch.x + ch.width);
                renderBottom = Math.max(renderBottom, ch.y + ch.height);
            }
        }
        for (var i = 0; i < _rubies.count; ++i) {
            var ruby = _rubies[i]; if (ruby.start >= _chars.count) continue;
            var first = _chars[ruby.start], end = Math.min(_chars.count, ruby.start + ruby.count);
            while (end > ruby.start + 1 && _chars[end - 1].line != first.line) --end;
            var last = _chars[end - 1];
            var width = _measure(ruby.text, ruby.size, ruby.style);
            var span = vertical ? last.y + last.height - first.y : last.x + last.width - first.x;
            first.ruby = [%["text" => ruby.text, "size" => ruby.size, "face" => ruby.style.face,
                "x" => vertical ? first.width - ruby.offset : (span - width) / 2,
                "y" => vertical ? (span - width) / 2 : -ruby.size + ruby.offset]];
            if (!vertical) renderTop = Math.min(renderTop, first.y - ruby.size + ruby.offset);
        }
        renderOver = !_all && blockLimit > 0 && extent > blockLimit;
        _dirty = false;
    }
    property renderCount { getter() { return _chars.count; } }
    property renderLines { getter() { return _lines.count + (_lineStart < _chars.count ? 1 : 0); } }
    property renderDelay { getter() { return _clock * Math.max(0, _finite(timeScale)); } }
    property renderText { getter() { return _plain; } }
    property maxScrollOffset { getter() { done(); return Math.max(0, vertical ? renderRight - _width : renderBottom - _height); } }
    property maxScrollLine {
        getter() {
            var offset = maxScrollOffset, line = 0;
            while (line < _lines.count && _lines[line].block < offset) ++line;
            return line;
        }
    }
    function calcLineOffset(line) {
        line = int line; if (line <= 0) return 0;
        return line < _lines.count ? _lines[line].block : _block;
    }
    function calcShowCount(elapsed) {
        elapsed = Math.max(0, _finite(elapsed));
        if (timeScale <= 0) return _chars.count;
        var count = 0;
        while (count < _chars.count && _chars[count].delay * timeScale <= elapsed) ++count;
        return count;
    }
    function getKeyWait(start = 0) {
        var result = [];
        for (var i = 0; i < _keys.count; ++i) {
            if (_keys[i].pos < start) continue;
            result.add(%["pos" => _keys[i].pos, "time" => _keys[i].time * Math.max(0, _finite(timeScale))]);
        }
        return result;
    }
    function getCharacters(start = 0, end = 0) {
        done(); start = Math.max(0, int start);
        end = end === void || end <= 0 ? _chars.count : Math.min(_chars.count, int end);
        var result = [];
        for (var i = start; i < end; ++i) {
            var ch = _copy(_chars[i]);
            ch.delay *= timeScale; ch.time = ch.delay;
            result.add(ch);
        }
        return result;
    }
    function getLinkNames() {
        var result = [];
        for (var i = 0; i < _chars.count; ++i) {
            var name = _chars[i].link;
            if (name !== void && result.find(name) < 0) result.add(name);
        }
        return result;
    }
    function getLinkCharacters(name) {
        var result = [], chars = getCharacters();
        for (var i = 0; i < chars.count; ++i) if (chars[i].link == name) result.add(chars[i]);
        return result;
    }
    function getLinkRects(name) {
        done(); var result = [], rectangle = void, lastLine = -1;
        for (var i = 0; i < _chars.count; ++i) {
            var ch = _chars[i];
            if (ch.link != name || ch.link === void) { rectangle = void; continue; }
            if (rectangle === void || ch.line != lastLine) {
                rectangle = %["left" => ch.x, "top" => ch.y, "right" => ch.x + ch.width, "bottom" => ch.y + ch.height];
                result.add(rectangle);
            } else {
                rectangle.left = Math.min(rectangle.left, ch.x); rectangle.top = Math.min(rectangle.top, ch.y);
                rectangle.right = Math.max(rectangle.right, ch.x + ch.width); rectangle.bottom = Math.max(rectangle.bottom, ch.y + ch.height);
            }
            rectangle.width = rectangle.right - rectangle.left; rectangle.height = rectangle.bottom - rectangle.top;
            lastLine = ch.line;
        }
        return result;
    }
    function isLinkContains(name, x, y) {
        var rectangles = getLinkRects(name);
        for (var i = 0; i < rectangles.count; ++i) {
            var r = rectangles[i]; if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return true;
        }
        return false;
    }
    function getLinkOfPosition(x, y) {
        var names = getLinkNames();
        for (var i = 0; i < names.count; ++i) if (isLinkContains(names[i], x, y)) return names[i];
        return void;
    }
    function contains(x, y) {
        done(); return x >= renderLeft && x < renderRight && y >= renderTop && y < renderBottom;
    }
}
)K2TEXT")
