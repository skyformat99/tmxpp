#include <fstream>
#include <string>
#include <type_traits>
#include <variant>
#include <boost/hana/functional/overload.hpp>
#include <tmxpp.hpp>
#include <tmxpp/exceptions.hpp>
#include <tmxpp/impl/Xml.hpp>
#include <tmxpp/impl/exceptions.hpp>
#include <tmxpp/impl/tmx_info.hpp>
#include <tmxpp/impl/to_string_flipped_ids.hpp>
#include <tmxpp/impl/write_poly.hpp>
#include <tmxpp/impl/write_utility.hpp>

namespace tmxpp {

namespace impl {

using namespace tmx_info;

template <class Number>
void write(Size<Number> sz, Xml::Element elem)
{
    add(elem, size_width, sz.w);
    add(elem, size_height, sz.h);
}

void write_tile(pxSize sz, Xml::Element elem)
{
    add(elem, tile_size_width, sz.w);
    add(elem, tile_size_height, sz.h);
}

// Properties ------------------------------------------------------------------

void write(const Property::Value& value, Xml::Element prop)
{
    auto add = [=](Xml::Attribute::Value alternative, auto value) {
        prop.add(property_alternative, alternative);
        impl::add(prop, property_value, value);
    };

    std::visit(
        boost::hana::overload(
            [=](int i) { add(property_alternative_int, i); },
            [=](double d) {
                add(property_alternative_double, to_string<double>(d));
            },
            [=](bool b) {
                add(property_alternative_bool,
                    get(b ? property_value_true : property_value_false));
            },
            [=](Color c) { add(property_alternative_color, c); },
            [=](File f) { add(property_alternative_file, f.string()); },
            [=](const std::string& s) {
                if (bool is_multiline{s.find('\n') != std::string::npos})
                    prop.value(Xml::Element::Value{s});
                else
                    impl::add(prop, property_value, s);
            }),
        value);
}

void write(const Property& p, Xml::Element elem)
{
    add(elem, property_name, p.name);
    write(p.value, elem);
}

void write(const Properties& ps, Xml::Element parent)
{
    if (ps.empty())
        return;

    auto elem{parent.add(properties)};

    for (const auto& p : ps)
        write(p, elem.add(property));
}

// Image -----------------------------------------------------------------------

void write(const Image& img, Xml::Element elem)
{
    add(elem, image_source, img.source.string());
    add(elem, image_transparent, img.transparent);
    write(img.size, elem);
}

// Animation -------------------------------------------------------------------

void write(Frame f, Xml::Element elem)
{
    add(elem, frame_id, f.id);
    add(elem, frame_duration, f.duration->count());
}

void write(const Animation& anim, Xml::Element tile)
{
    if (anim.empty())
        return;

    auto elem{tile.add(animation)};

    for (const auto& f : anim)
        write(f, elem.add(frame));
}

// Map::Tile_set ---------------------------------------------------------------

void write_tile(Offset o, Xml::Element tset)
{
    if (o == Offset{})
        return;

    auto elem{tset.add(tile_offset)};

    add(elem, tile_offset_x, o.x);
    add(elem, tile_offset_y, o.y);
}

void write(const Object_layer& l, Xml::Element elem);

template <class Tile>
std::enable_if_t<
    std::is_same_v<Tile, Tile_set::Tile> ||
    std::is_same_v<Tile, Image_collection::Tile>>
write(const Tile& tile, Xml::Element elem)
{
    add(elem, tile_set_tile_id, tile.id);
    write(tile.properties, elem);
    // clang-format off
    if constexpr (std::is_same_v<Tile, Image_collection::Tile>)
        write(tile.image, elem.add(image));
    // clang-format on
    if (const auto& cs{tile.collision_shape})
        write(*cs, elem.add(object_layer));
    write(tile.animation, elem);
}

template <class Tiles>
std::enable_if_t<
    std::is_same_v<Tiles, Tile_set::Tiles> ||
    std::is_same_v<Tiles, Image_collection::Tiles>>
write(const Tiles& ts, Xml::Element tset)
{
    for (const auto& t : ts)
        write(t, tset.add(tile_set_tile));
}

enum class Tile_set_type : unsigned char { unknown, tsx };

template <class Tset>
void map_tile_set_visitor(
    const Tset& ts, Xml::Element elem, Tile_set_type type,
    const std::experimental::filesystem::path& tsx_base = {})
{
    if (type != Tile_set_type::tsx) {
        add(elem, tile_set_first_id, ts.first_id);
        non_empty_add(elem, tile_set_tsx, ts.tsx);
    }

    if (bool is_external{type == Tile_set_type::unknown && !ts.tsx.empty()})
        return tmxpp::write(ts, tsx_base);

    add(elem, tile_set_name, ts.name);
    // clang-format off
    if constexpr (std::is_same_v<Tset, Tile_set>) {
        write_tile(ts.tile_size, elem);
        non_default_add(elem, tile_set_spacing, ts.spacing);
        non_default_add(elem, tile_set_margin, ts.margin);
        add(elem, tile_set_tile_count, *ts.size.w * *ts.size.h);
        add(elem, tile_set_columns, ts.size.w);
    }
    else {
        write_tile(ts.max_tile_size, elem);
        add(elem, tile_set_tile_count, ts.tile_count);
        add(elem, tile_set_columns, ts.columns);
    };
    write_tile(ts.tile_offset, elem);
    write(ts.properties, elem);
    if constexpr (std::is_same_v<Tset, Tile_set>)
        write(ts.image, elem.add(image));
    // clang-format on
    write(ts.tiles, elem);
}

template <class Tile_set_>
std::enable_if_t<
    std::is_same_v<Tile_set_, Tile_set> ||
    std::is_same_v<Tile_set_, Image_collection>>
write(const Tile_set_& ts, Xml::Element elem, Tile_set_type type)
{
    map_tile_set_visitor(ts, elem, type);
}

void write(
    const Map::Tile_set& ts, Xml::Element elem, Tile_set_type type,
    const std::experimental::filesystem::path& tsx_base = {})
{
    std::visit(
        [elem, type, &tsx_base](const auto& ts) {
            map_tile_set_visitor(ts, elem, type, tsx_base);
        },
        ts);
}

// Data ------------------------------------------------------------------------

void write(Data::Encoding e, Xml::Element data)
{
    data.add(data_encoding, [e] {
        switch (e) {
        case Data::Encoding::csv: return data_encoding_csv;
        case Data::Encoding::base64: return data_encoding_base64;
        default: throw Invalid_enum{"Invalid Data::Encoding.", e};
        }
    }());
}

void write(Data::Compression c, Xml::Element data)
{
    if (c == Data::Compression::none)
        return;

    data.add(data_compression, [c] {
        switch (c) {
        case Data::Compression::zlib: return data_compression_zlib;
        default: throw Invalid_enum{"Invalid Data::Compression.", c};
        }
    }());
}

void write(Data::Format f, Xml::Element data)
{
    write(f.encoding(), data);
    write(f.compression(), data);
}

void write(const Data& d, Xml::Element elem, iSize size)
{
    if (d.format != Data::Encoding::csv)
        throw Exception{"Can only handle csv-encoded data."};

    write(d.format, elem);
    elem.value(to_string(d.ids, size));
}

// Object ----------------------------------------------------------------------

void write(Point p, Xml::Element obj)
{
    add(obj, point_x, p.x);
    add(obj, point_y, p.y);
}

void write_object(pxSize sz, Xml::Element obj)
{
    non_default_add(obj, size_width, sz.w);
    non_default_add(obj, size_height, sz.h);
}

void write(const Object::Shape& s, Xml::Element obj)
{
    std::visit(
        boost::hana::overload(
            [obj](Object::Rectangle r) { write_object(r.size, obj); },
            [obj](Object::Ellipse e) {
                write_object(e.size, obj);
                obj.add(object_ellipse);
            },
            [obj](const auto& poly) { write(poly, obj); }),
        s);
}

void write(const Object& obj, Xml::Element elem)
{
    add(elem, object_unique_id, obj.unique_id);
    non_empty_add(elem, object_name, obj.name);
    non_empty_add(elem, object_type, obj.type);
    add(elem, object_global_id, obj.global_id);
    write(obj.position, elem);
    write(obj.shape, elem);
    non_default_add(elem, object_clockwise_rotation, obj.clockwise_rotation);
    if (!obj.visible)
        add(elem, object_visible, "0");
    write(obj.properties, elem);
}

// Map::Layer ------------------------------------------------------------------

void write(Object_layer::Draw_order do_, Xml::Element layer)
{
    if (do_ == Object_layer::Draw_order::top_down)
        return;

    layer.add(object_layer_draw_order, [do_] {
        switch (do_) {
        case Object_layer::Draw_order::index:
            return object_layer_draw_order_index;
        default: throw Invalid_enum{"Invalid Object_layer::Draw_order.", do_};
        }
    }());
}

void write(Offset o, Xml::Element layer)
{
    if (o == Offset{})
        return;

    add(layer, offset_x, o.x);
    add(layer, offset_y, o.y);
}

void write(const Object_layer::Objects& objs, Xml::Element elem)
{
    for (const auto& obj : objs)
        write(obj, elem.add(object));
}

template <class Layer>
void layer_visitor(const Layer& l, Xml::Element elem)
{
    // clang-format off
    if constexpr (std::is_same_v<Layer, Object_layer>) {
        add(elem, object_layer_color, l.color);
        write(l.draw_order, elem);
    }
    non_empty_add(elem, layer_name, l.name);
    if constexpr (std::is_same_v<Layer, Tile_layer>)
        write(l.size, elem);
    if (!l.visible)
        add(elem, layer_visible, "0");
    non_default_add(elem, layer_opacity, l.opacity, Default{Unit_interval{1}});
    write(l.offset, elem);
    if constexpr (std::is_same_v<Layer, Image_layer>)
        if (auto img{l.image})
            write(*img, elem.add(image));
    write(l.properties, elem);
    if constexpr (std::is_same_v<Layer, Tile_layer>)
        write(l.data, elem.add(data), l.size);
    if constexpr (std::is_same_v<Layer, Object_layer>)
        write(l.objects, elem);
    // clang-format on
}

void write(const Object_layer& l, Xml::Element elem)
{
    layer_visitor(l, elem);
}

void write(const Map::Layer& l, Xml::Element map)
{
    std::visit(
        [map](const auto& l) {
            auto elem{map.add([] {
                using Layer = std::decay_t<decltype(l)>;
                // clang-format off
                if constexpr (std::is_same_v<Layer, Tile_layer>)
                    return tile_layer;
                if constexpr (std::is_same_v<Layer, Object_layer>)
                    return object_layer;
                if constexpr (std::is_same_v<Layer, Image_layer>)
                    return image_layer;
                // clang-format on
            }())};

            layer_visitor(l, elem);
        },
        l);
}

// Map -------------------------------------------------------------------------

void write(Map::Render_order ro, Xml::Element map)
{
    map.add(map_render_order, [ro] {
        switch (ro) {
        case Map::Render_order::right_down: return map_render_order_right_down;
        case Map::Render_order::right_up: return map_render_order_right_up;
        case Map::Render_order::left_down: return map_render_order_left_down;
        case Map::Render_order::left_up: return map_render_order_left_up;
        default: throw Invalid_enum{"Invalid Map::Render_order.", ro};
        }
    }());
}

void write(Map::Staggered::Axis a, Xml::Element map)
{
    map.add(map_staggered_axis, [a] {
        switch (a) {
        case Map::Staggered::Axis::x: return map_staggered_axis_x;
        case Map::Staggered::Axis::y: return map_staggered_axis_y;
        default: throw Invalid_enum{"Invalid Map::Staggered::Axis.", a};
        }
    }());
}

void write(Map::Staggered::Index i, Xml::Element map)
{
    map.add(map_staggered_index, [i] {
        switch (i) {
        case Map::Staggered::Index::even: return map_staggered_index_even;
        case Map::Staggered::Index::odd: return map_staggered_index_odd;
        default: throw Invalid_enum{"Invalid Map::Staggered::Index.", i};
        }
    }());
}

void write(Map::Staggered s, Xml::Element map)
{
    write(s.axis, map);
    write(s.index, map);
}

void write(Map::Hexagonal h, Xml::Element map)
{
    add(map, map_hexagonal_side_legth, h.side_length);
    write(static_cast<Map::Staggered>(h), map);
}

void write(
    Map::Orientation orient, Map::Render_order render_order, Xml::Element map)
{
    auto add = [=](Xml::Attribute::Value orient) {
        map.add(map_orientation, orient);

        write(render_order, map);
    };

    std::visit(
        boost::hana::overload(
            [=](Map::Orthogonal) { add(map_orthogonal); },
            [=](Map::Isometric) { add(map_isometric); },
            [=](Map::Staggered s) {
                add(map_staggered);
                write(s, map);
            },
            [=](Map::Hexagonal h) {
                add(map_hexagonal);
                write(h, map);
            }),
        orient);
}

void write(
    const Map::Tile_sets& tses, Xml::Element map,
    const std::experimental::filesystem::path& tsx_base)
{
    for (const auto& ts : tses)
        write(ts, map.add(tile_set), Tile_set_type::unknown, tsx_base);
}

void write(const Map::Layers& ls, Xml::Element map)
{
    for (const auto& l : ls)
        write(l, map);
}

void write(
    const Map& map, Xml::Element elem,
    const std::experimental::filesystem::path& tsx_base)
{
    add(elem, map_version, map.version);
    write(map.orientation, map.render_order, elem);
    write(map.size, elem);
    write_tile(map.general_tile_size, elem);
    add(elem, map_background, map.background);
    add(elem, map_next_id, map.next_id);
    write(map.properties, elem);
    write(map.tile_sets, elem, tsx_base);
    write(map.layers, elem);
}

void write(const Xml& xml, const std::experimental::filesystem::path& out)
{
    std::ofstream ofs{out};

    if (!ofs)
        throw Exception{"Output path " + out.string() + " presented problems."};

    ofs << xml;
}

} // namespace impl

void write(const Map& map, const std::experimental::filesystem::path& path)
{
    impl::Xml tmx{impl::map};

    impl::write(map, tmx.root(), path.parent_path());

    write(tmx, path);
}

template <class Tile_set_>
std::enable_if_t<
    std::is_same_v<Tile_set_, Map::Tile_set> ||
    std::is_same_v<Tile_set_, Tile_set> ||
    std::is_same_v<Tile_set_, Image_collection>>
write(const Tile_set_& ts, const std::experimental::filesystem::path& base)
{
    if (ts.tsx.empty())
        throw Exception{
            "Writing an external tile set requires a non-empty tsx."};

    impl::Xml tsx{impl::tile_set};

    impl::write(ts, tsx.root(), impl::Tile_set_type::tsx);

    write(tsx, absolute(ts.tsx, base));
}

} // namespace tmxpp
