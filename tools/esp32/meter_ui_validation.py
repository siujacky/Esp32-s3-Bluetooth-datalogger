"""Semantic checks for the version-1 UI map, including the actual canvas.

The board compiler checks types/keys first. These checks mirror the portable
layout's integer scaling and keyboard packing so bad maps fail before upload.
"""


def validate_usable_ui(ui, display, style=0):
    def require(condition, message):
        if not condition:
            raise ValueError(message)

    def overlap(a, b):
        return a[0] < b[0] + b[2] and b[0] < a[0] + a[2] and a[1] < b[1] + b[3] and b[1] < a[1] + a[3]

    for name in ('reading','setup','advanced'):
        view=ui[name]
        width, height = display['width'], display['height']
        if view['rotation'] & 1:
            width, height = height, width
        rw, rh = view['reference_width'], view['reference_height']
        prefix = 'ui.' + name

        def x(n):
            return n * width // rw

        def y(n):
            return n * height // rh

        def xl(n):
            return max(1, x(n)) if n > 0 else 0

        def yl(n):
            return max(1, y(n)) if n > 0 else 0

        def font(n):
            return min(8, max(1, n * min(width * 1024 // rw, height * 1024 // rh) // 1024)) if n > 0 else 0

        def rectangle(value, path):
            a, b = x(value['x']), y(value['y'])
            rect = (a, b, x(value['x'] + value['w']) - a, y(value['y'] + value['h']) - b)
            require(rect[2] > 0 and rect[3] > 0 and a >= 0 and b >= 0 and a + rect[2] <= width and b + rect[3] <= height,
                    path + ' disappears or exceeds the actual canvas')
            if view['safe_circle']:
                radius = min(width, height) - 2 * max(xl(view['safe_margin']), yl(view['safe_margin']))
                require(all((2 * cx - width) ** 2 + (2 * cy - height) ** 2 <= radius ** 2
                            for cx in (a, a + rect[2]) for cy in (b, b + rect[3])), path + ' exceeds the safe circle')
            return rect

        def positive(value, fields, path):
            for field in fields:
                require(value[field] > 0, path + '.' + field + ' must be positive')

        def text_size(value, path):
            require(1 <= value <= 8, path + ' must be 1..8')

        def label(value, path, maximum_y=height, offset_y=0):
            if not value['size']:
                return
            require(0 <= x(value['x']) <= width and 0 <= offset_y + y(value['y']) and offset_y + y(value['y']) + 8 * font(value['size']) <= maximum_y,
                    path + ' exceeds the available text area')

        if name == 'reading':
            overview=ui['overview']
            for key in ('values','scope'):
                area=rectangle(overview[key],'ui.overview.'+key)
                require(area[3]-2*yl(overview['gap'])>=3*24,'ui.overview.'+key+' cannot fit three readable lanes')
            if overview['show_count_button']:
                button=rectangle(overview['count_button'],'ui.overview.count_button')
                require(button[2]>=44 and button[3]>=32,'ui.overview.count_button is too small')
                require(not overlap(button,rectangle(overview['values'],'ui.overview.values')) and not overlap(button,rectangle(overview['scope'],'ui.overview.scope')),'ui.overview count button overlaps a lane viewport')
            spec = view['value']
            positive(spec, ('baseline', 'max_width', 'size_x', 'size_y'), prefix + '.value')
            text_size(spec['size_x'], prefix + '.value.size_x');text_size(spec['size_y'], prefix + '.value.size_y')
            require(spec['baseline'] < rh and spec['max_width'] <= rw, prefix + '.value exceeds its reference canvas')
            # FreeSansBold24pt7b: digits/OL have a top offset down to -34
            # and extend at most two pixels below their baseline at size 1.
            require(34 * font(spec['size_y']) <= y(spec['baseline']) and y(spec['baseline']) + 2 * font(spec['size_y']) <= height,
                    prefix + '.value baseline clips its numeric font')
            rectangle(view['scope']['plot'], prefix + '.scope.plot')
            for key in ('profile', 'status', 'flags', 'unit', 'slot'):
                label(view[key], prefix + '.' + key)
            for key in ('value', 'status', 'mode', 'slot'):
                label(view['scope'][key], prefix + '.scope.' + key)
        elif name == 'setup':
            viewport = rectangle(view['viewport'], prefix + '.viewport')
            header = rectangle(view['header'], prefix + '.header')
            require(header[1] + header[3] <= viewport[1], prefix + '.header must stay above the scrolling viewport')
            qr = rectangle(view['network']['qr'], prefix + '.network.qr')
            require(min(qr[2], qr[3]) >= 65, prefix + '.network.qr cannot fit the largest supported Wi-Fi QR with its quiet zone')
            row = view['row']
            positive(row, ('width', 'height', 'step', 'font_size'), prefix + '.row')
            require(1 <= row['columns'] <= 4, prefix + '.row.columns must be 1..4')
            card_width=(xl(row['width'])-(row['columns']-1)*xl(row['column_gap']))//row['columns']
            require(card_width >= 12 + 6 * font(row['font_size']), prefix + '.row columns/gap remove the label area')
            if row['columns']>1:require(card_width>=96 and yl(row['height'])>=44, prefix + '.row grid cards are too small')
            if style==1:require(yl(row['height'])-yl(24)>=yl(10)+yl(28)+yl(4),prefix+'.row modern title/detail areas overlap')
            text_size(row['font_size'], prefix + '.row.font_size')
            text_size(view['header_font_size'], prefix + '.header_font_size')
            require(8 * font(view['header_font_size']) + 12 <= header[3], prefix + '.header font exceeds the control')
            require(row['height'] <= row['step'], prefix + '.row height exceeds its step')
            require(row['x'] >= view['viewport']['x'] and row['x'] + row['width'] <= view['viewport']['x'] + view['viewport']['w'], prefix + '.row exceeds the viewport width')
            require(yl(row['height']) <= viewport[3] and xl(row['width']) >= 12 + 6 * font(row['font_size']), prefix + '.row is unusable at the actual size')
            require(18 * font(row['font_size']) <= yl(row['height']), prefix + '.row cannot fit its two-line label')
            for key, value in view['info'].items():
                label(value, prefix + '.info.' + key, yl(view['info_height']))
            label(view['footer'], prefix + '.footer')
        else:
            viewport = rectangle(view['viewport'], prefix + '.viewport')
            fixed = [rectangle(view[key], prefix + '.' + key) for key in ('back', 'up', 'down')]
            require(all(r[2] >= 24 and r[3] >= 24 for r in fixed), prefix + '.fixed navigation targets must be at least 24 pixels')
            require(fixed[0][1] + fixed[0][3] <= viewport[1], prefix + '.back must stay above the viewport')
            require(all(r[1] >= viewport[1] + viewport[3] for r in fixed[1:]), prefix + '.footer controls must stay below the viewport')
            require(not overlap(fixed[1], fixed[2]), prefix + '.footer controls overlap')
            header = view['header']
            label(header, prefix + '.header', viewport[1])
            require(y(header['y']) + 16 * font(header['size']) <= viewport[1], prefix + '.header needs room for two text lines')
            label(view['message'], prefix + '.message')
            row = view['row']
            positive(row, ('step', 'title_size', 'detail_size', 'info_size'), prefix + '.row')
            for field in ('title_size', 'detail_size', 'info_size'):
                text_size(row[field], prefix + '.row.' + field)
            require(row['gap'] < row['step'], prefix + '.row gap removes the whole control')
            row_height = yl(row['step']) - yl(row['gap']) if row['gap'] else yl(row['step'])
            require(yl(row['step']) <= viewport[3], prefix + '.row cannot fit in viewport')
            padding = xl(row['padding'])
            require(viewport[2] - 2 * padding >= 6 * max(font(row['title_size']), font(row['detail_size']), font(row['info_size'])), prefix + '.row padding removes its text area')
            require(y(row['title_y']) + 16 * font(row['title_size']) <= y(row['detail_y']) and y(row['detail_y']) + 8 * font(row['detail_size']) <= row_height,
                    prefix + '.row text overlaps or exceeds the control')
            require(yl(7) + 48 * font(row['info_size']) <= row_height, prefix + '.row information text exceeds the control')
            key = view['keyboard']
            positive(key, ('preferred_columns', 'minimum_key_width', 'minimum_key_height', 'control_height', 'footer_height', 'font_size'), prefix + '.keyboard')
            require(key['preferred_columns'] in (1, 2, 4, 8, 16), prefix + '.keyboard.preferred_columns must divide 16')
            text_size(key['font_size'], prefix + '.keyboard.font_size')
            gap = max(1, min(xl(key['gap']), yl(key['gap'])))
            min_width, min_height = xl(key['minimum_key_width']), yl(key['minimum_key_height'])
            require(min_width >= 16 and min_height >= 16, prefix + '.keyboard touch targets become smaller than 16 pixels')
            require(6 * font(key['font_size']) <= min_width and 8 * font(key['font_size']) <= min_height, prefix + '.keyboard font exceeds its keys')
            remaining = viewport[3] - yl(key['caption_height']) - yl(key['text_height']) - 3 * gap
            fits = False
            for columns in (key['preferred_columns'], 8, 4, 16, 2, 1):
                rows = 16 // columns
                if viewport[2] // columns - gap < min_width or viewport[2] // 4 - gap < min_width:
                    continue
                required = max(min_height, yl(key['control_height'])) + max(min_height, yl(key['footer_height'])) + rows * (min_height + gap)
                if remaining >= required:
                    fits = True
                    break
            require(fits, prefix + '.keyboard controls do not fit the actual viewport')
            grid=view['home_grid']
            positive(grid,('columns','step','height'),prefix+'.home_grid')
            require(1<=grid['columns']<=4 and grid['height']<=grid['step'],prefix+'.home_grid has invalid columns or overlapping rows')
            cell_width=(viewport[2]-(grid['columns']-1)*xl(grid['column_gap']))//grid['columns']
            require(cell_width>=24 and yl(grid['height'])<=viewport[3],prefix+'.home_grid exceeds the viewport')
            if grid['columns']>1:require(cell_width>=96 and yl(grid['height'])>=44,prefix+'.home_grid cards are too small')
            if style==1 and grid['columns']>1:require(yl(grid['height'])>=yl(66)+8*font(row['detail_size'])+yl(8),prefix+'.home_grid modern details exceed the card')
