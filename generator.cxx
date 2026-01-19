#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <algorithm>
#include <stdexcept>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::ordered_json;

namespace {

struct Block
{
  std::string key;
  std::string header;
  std::string data;
  int top = 0;
  int left = 0;
  int width = 0;
  int height = 0;
  int margin_left = 0;
  int margin_right = 0;
};

struct RowGroup
{
  int top = 0;
  int height = 0;
  std::vector<Block> blocks;
};

int parse_int(json const& value, std::string const& what)
{
  if (value.is_number_integer())
    return value.get<int>();
  if (value.is_string())
  {
    std::string const s = value.get<std::string>();
    std::size_t parsed = 0;
    int const out = std::stoi(s, &parsed, 10);
    if (parsed != s.size())
      throw std::runtime_error(what + " must be an integer, got '" + s + "'");
    return out;
  }
  throw std::runtime_error(what + " must be an integer or integer string");
}

int data_width(std::string const& data)
{
  if (data == "grid36")
    return 37;
  if (data == "grid10")
    return 10;
  return static_cast<int>(data.size());
}

int data_height(std::string const& data)
{
  if (data == "grid36")
    return 31;
  if (data == "grid10")
    return 11;
  return 2;
}

std::string html_escape(std::string const& s)
{
  std::string out;
  out.reserve(s.size());
  for (char const ch : s)
  {
    switch (ch)
    {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case ' ': out += "&nbsp;"; break;
      default: out += ch; break;
    }
  }
  return out;
}

void write_empty_span(std::ostream& out, int colspan)
{
  if (colspan <= 0)
    return;

  out << "\t\t<td";
  if (colspan > 1)
    out << " colspan=" << colspan;
  out << "><br></td>\n";
}

void write_block_header_row(std::ostream& out, Block const& block)
{
  write_empty_span(out, block.margin_left);

  out << "\t\t<td";
  int const dwidth = data_width(block.data);
  if (dwidth > 1)
    out << " colspan=" << dwidth;
  out << ">" << html_escape(block.header) << "</td>\n";

  write_empty_span(out, block.margin_right);
}

void write_block_data_row(std::ostream& out, Block const& block, int data_row_index)
{
  write_empty_span(out, block.margin_left);

  if (block.data == "grid10")
  {
    std::string const row = "0123456789";
    for (char const ch : row)
      out << "\t\t<td>" << html_escape(std::string(1, ch)) << "</td>\n";
  }
  else if (block.data == "grid36")
  {
    int const pattern_index = data_row_index % 5;
    if (pattern_index < 4)
    {
      std::string const row = "-ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
      for (char const ch : row)
        out << "\t\t<td>" << html_escape(std::string(1, ch)) << "</td>\n";
    }
    else
    {
      out << "\t\t<td>-</td>\n";
      out << "\t\t<td colspan=36></td>\n";
    }
  }
  else
  {
    if (data_row_index != 0)
      throw std::runtime_error("internal error: unexpected data_row_index for non-grid block '" + block.key + "'");
    for (char const ch : block.data)
      out << "\t\t<td>" << html_escape(std::string(1, ch)) << "</td>\n";
  }

  write_empty_span(out, block.margin_right);
}

} // namespace

int main(int argc, char* argv[])
{
  if (argc != 2)
  {
    std::cerr << "Usage: " << argv[0] << " <basename>\n";
    std::cerr << "  Input is read from <basename>.json\n";
    std::cerr << "  Output will be written to <basename>.html\n";
    return 1;
  }

  std::string const basename = argv[1];
  std::string const input_filename = basename + ".json";
  std::string const output_filename = basename + ".html";

  namespace fs = std::filesystem;
  fs::path const input_file_path(input_filename);
  fs::path const output_file_path(output_filename);

  if (!fs::exists(input_file_path))
  {
    std::cerr << "Expected input file " << input_file_path << " does not exist.\n";
    return 1;
  }

  try
  {
    std::ifstream input_file(input_file_path);
    json j = json::parse(input_file);

    std::string const title_left = j.at("title").at("left").get<std::string>();
    std::string const title_right = j.at("title").at("right").get<std::string>();
    int const table_width = parse_int(j.at("table").at("width"), "table.width");

    std::cout << "title.left: " << title_left << "\n";
    std::cout << "title.right: " << title_right << "\n";
    std::cout << "table.width: " << table_width << "\n\n";

    json const& headers = j.at("data_headers");
    json const& data = j.at("data");
    json const& margins = j.at("margins");

    if (!headers.is_object())
      throw std::runtime_error("data_headers must be an object");
    if (!data.is_object())
      throw std::runtime_error("data must be an object");
    if (!margins.is_object())
      throw std::runtime_error("margins must be an object");

    int cursor_left = 0;
    int cursor_top = 0;
    int current_row_height = 0;

    std::vector<RowGroup> groups;
    RowGroup current_group;
    current_group.top = 0;

    for (auto const& [key, header_value] : headers.items())
    {
      if (!data.contains(key))
        throw std::runtime_error("data_headers key '" + key + "' is missing from data");
      if (!margins.contains(key))
        throw std::runtime_error("data_headers key '" + key + "' is missing from margins");

      std::string const header = header_value.get<std::string>();
      std::string const data_value = data.at(key).get<std::string>();
      json const& margin_obj = margins.at(key);

      if (!margin_obj.is_object())
        throw std::runtime_error("margins." + key + " must be an object");

      int const margin_left = margin_obj.contains("left") ? parse_int(margin_obj.at("left"), "margins." + key + ".left") : 0;
      int const margin_right = margin_obj.contains("right") ? parse_int(margin_obj.at("right"), "margins." + key + ".right") : 0;

      int const width = data_width(data_value) + margin_left + margin_right;
      int const height = data_height(data_value);

      if (width > table_width)
        throw std::runtime_error("block '" + key + "' has width " + std::to_string(width) + " > table width " + std::to_string(table_width));

      if (cursor_left + width > table_width)
      {
        current_group.height = current_row_height;
        groups.push_back(std::move(current_group));
        current_group = RowGroup{};
        current_group.top = cursor_top + current_row_height;

        cursor_top += current_row_height;
        cursor_left = 0;
        current_row_height = 0;
      }

      int const block_left = cursor_left;
      int const block_top = cursor_top;
      cursor_left += width;
      current_row_height = std::max(current_row_height, height);

      Block block;
      block.key = key;
      block.header = header;
      block.data = data_value;
      block.top = block_top;
      block.left = block_left;
      block.width = width;
      block.height = height;
      block.margin_left = margin_left;
      block.margin_right = margin_right;
      current_group.blocks.push_back(block);

      std::cout << key << ": header='" << header << "' data='" << data_value << "' top=" << block_top << " left=" << block_left << " width=" << width
                << " height=" << height << "\n";
    }

    if (!current_group.blocks.empty())
    {
      current_group.height = current_row_height;
      groups.push_back(std::move(current_group));
    }

    std::ofstream output_file(output_file_path);
    if (!output_file)
      throw std::runtime_error("unable to open output file " + output_file_path.string());

    output_file << R"(<!DOCTYPE html>
<!-- Print from Firefox (control-P) Portrait, Paper size A4, Scale 90%, Margins Default, Print headers and footers OFF -->
<html>
<head>
  <meta http-equiv="content-type" content="text/html; charset=utf-8"/>
  <title>passphrase</title>
  <link rel="stylesheet" href="sheet.css">
</head>
<body>
<div class="sheet">
<h1 class="title">
)";

    output_file << "  <span>" << html_escape(title_left) << "</span>\n";
    output_file << "  <span>" << html_escape(title_right) << "</span>\n";
    output_file << "</h1>\n";

    output_file << "<table cellspacing=\"0\" border=\"0\">\n";
    output_file << "\t<colgroup span=\"" << table_width << "\" width=\"25\"></colgroup>\n";

    for (RowGroup const& group : groups)
    {
      std::vector<Block> blocks = group.blocks;
      std::sort(blocks.begin(), blocks.end(), [](Block const& a, Block const& b) { return a.left < b.left; });

      int used_width = 0;
      for (Block const& b : blocks)
        used_width += b.width;
      if (used_width > table_width)
        throw std::runtime_error("internal error: row group width exceeds table width");

      for (int row_offset = 0; row_offset < group.height; ++row_offset)
      {
        if (row_offset == 0)
          output_file << "\t<tr class=\"header\">\n";
        else
          output_file << "\t<tr>\n";

        for (Block const& block : blocks)
        {
          if (row_offset == 0)
          {
            write_block_header_row(output_file, block);
          }
          else if (row_offset < block.height)
          {
            write_block_data_row(output_file, block, row_offset - 1);
          }
          else
          {
            write_empty_span(output_file, block.width);
          }
        }

        write_empty_span(output_file, table_width - used_width);
        output_file << "\t</tr>\n";
      }
    }

    output_file << "</table>\n</div>\n</body>\n</html>\n";
    std::cout << "\nWrote " << output_file_path << "\n";
  }
  catch (std::exception const& e)
  {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}
