#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <algorithm>
#include <stdexcept>
#include <vector>
#include <cctype>
#include <nlohmann/json.hpp>

constexpr int grid10_height = 8;

using json = nlohmann::ordered_json;

namespace {

struct Block
{
  std::string key;
  std::string header;
  std::string data;
  std::string keyid_hex16;
  int width = 0;
  int content_width = 0;
  int height = 0;
  int margin_left = 0;
  int margin_right = 0;
  bool keyid_compact = false;
};

static std::vector<Block>* g_blocks = nullptr;

class BlocksScope
{
public:
  explicit BlocksScope(std::vector<Block>& blocks)
      : m_prev(g_blocks)
  {
    g_blocks = &blocks;
  }

  ~BlocksScope() { g_blocks = m_prev; }

  BlocksScope(BlocksScope const&) = delete;
  BlocksScope& operator=(BlocksScope const&) = delete;

private:
  std::vector<Block>* m_prev = nullptr;
};

Block const& get_block(int index)
{
  if (!g_blocks)
    throw std::runtime_error("internal error: blocks not initialized");
  return g_blocks->at(static_cast<std::size_t>(index));
}

Block& get_block_mut(int index)
{
  if (!g_blocks)
    throw std::runtime_error("internal error: blocks not initialized");
  return g_blocks->at(static_cast<std::size_t>(index));
}

class RowGroup
{
public:
  struct Column
  {
    int width = 0;
    int height = 0;
    std::vector<int> blocks;
  };

  explicit RowGroup(int table_width, int height = 0)
      : m_table_width(table_width)
      , m_height(height)
  {
  }

  [[nodiscard]] bool empty() const { return m_columns.empty(); }
  [[nodiscard]] int height() const { return m_height; }
  [[nodiscard]] int width() const
  {
    int w = 0;
    for (auto const& c : m_columns)
      w += c.width;
    return w;
  }

  [[nodiscard]] std::vector<int> blocks_in_order() const
  {
    std::vector<int> out;
    for (auto const& c : m_columns)
      out.insert(out.end(), c.blocks.begin(), c.blocks.end());
    return out;
  }

  [[nodiscard]] int last_block_index() const
  {
    if (m_columns.empty() || m_columns.back().blocks.empty())
      throw std::runtime_error("internal error: last_block_index on empty RowGroup");
    return m_columns.back().blocks.back();
  }

  [[nodiscard]] bool last_column_has_single_block() const
  {
    if (m_columns.empty())
      return false;
    return m_columns.back().blocks.size() == 1;
  }

  [[nodiscard]] Column const& last_column() const
  {
    if (m_columns.empty())
      throw std::runtime_error("internal error: last_column on empty RowGroup");
    return m_columns.back();
  }

  [[nodiscard]] std::vector<Column> const& columns() const { return m_columns; }

  bool add(int block_index)
  {
    Block const& b = get_block(block_index);
    if (m_columns.empty())
    {
      m_height = b.height;
      add_new_column(block_index);
      new_block_added(block_index);
      return true;
    }

    if (b.height > m_height)
    {
      int const new_height = b.height;
      RowGroup temp(m_table_width, new_height);

      std::vector<int> all = blocks_in_order();
      all.push_back(block_index);

      for (int const idx : all)
      {
        if (!temp.add_fixed_height(idx))
          return false;
      }

      *this = std::move(temp);
      new_block_added(block_index);
      return true;
    }

    if (add_to_last_column_if_fits(block_index) || add_new_column_if_fits(block_index))
    {
      new_block_added(block_index);
      return true;
    }
    return false;
  }

private:
  bool has_non_compact_keyid() const
  {
    if (m_keyid != -1)
    {
      Block const& b = get_block(m_keyid);
      return !b.keyid_compact;
    }
  }

  void new_block_added(int block_index)
  {
    Block const& b = get_block(block_index);
    if (!b.keyid_hex16.empty())
      m_keyid = block_index;
  }

  bool add_fixed_height(int block_index)
  {
    if (m_columns.empty())
    {
      add_new_column(block_index);
      return true;
    }

    if (add_to_last_column_if_fits(block_index))
      return true;
    if (add_new_column_if_fits(block_index))
      return true;
    return false;
  }

  bool add_to_last_column_if_fits(int block_index)
  {
    Column& col = m_columns.back();
    Block const& b = get_block(block_index);

    if (col.height + b.height > m_height)
      return false;

    int const new_col_width = std::max(col.width, b.width);
    int const new_total_width = width() - col.width + new_col_width;
    if (new_total_width > m_table_width)
      return false;

    col.blocks.push_back(block_index);
    col.height += b.height;
    col.width = new_col_width;
    return true;
  }

  bool add_new_column_if_fits(int block_index)
  {
    Block const& b = get_block(block_index);
    if (width() + b.width > m_table_width)
      return false;
    add_new_column(block_index);
    return true;
  }

  void add_new_column(int block_index)
  {
    Block const& b = get_block(block_index);
    Column col;
    col.width = b.width;
    col.height = b.height;
    col.blocks.push_back(block_index);
    m_columns.push_back(std::move(col));
  }

  int m_table_width = 0;
  int m_height = 0;
  std::vector<Column> m_columns;
  int m_keyid = -1;
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
    return 30;
  if (data == "grid10")
    return grid10_height + 1;
  return 2;
}

std::string parse_keyid_hex16(std::string const& s)
{
  std::string hex = s;
  if (hex.rfind("0x", 0) == 0 || hex.rfind("0X", 0) == 0)
    hex = hex.substr(2);

  if (hex.size() != 16)
    throw std::runtime_error("keyid must be optional '0x' followed by 16 hex characters");

  for (unsigned char const ch : hex)
  {
    if (!std::isxdigit(ch))
      throw std::runtime_error("keyid must be optional '0x' followed by 16 hex characters");
  }

  return hex;
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
      default: out += ch; break;
    }
  }
  return out;
}

void write_empty_span(std::ostream& out, int colspan)
{
  if (colspan <= 0)
    return;

  out << "\t\t<td colspan=" << colspan << "><br></td>\n";
}

void write_block_header_row(std::ostream& out, Block const& block)
{
  write_empty_span(out, block.margin_left);

  out << "\t\t<td class=\"header\" colspan=" << block.content_width << ">" << html_escape(block.header) << "</td>\n";

  write_empty_span(out, block.margin_right);
}

void write_block_data_row(std::ostream& out, Block const& block, int data_row_index)
{
  write_empty_span(out, block.margin_left);

  if (block.key == "keyid" || block.key == "keyid3")
  {
    if (block.keyid_compact)
    {
      if (data_row_index == 0)
      {
        out << "\t\t<td class=\"data\" colspan=2 rowspan=2>" << html_escape("0 x") << "</td>\n";
        for (int i = 0; i < 8; ++i)
          out << "\t\t<td class=\"data\">" << html_escape(std::string(1, block.keyid_hex16.at(i))) << "</td>\n";
      }
      else if (data_row_index == 1)
      {
        for (int i = 8; i < 16; ++i)
          out << "\t\t<td class=\"data\">" << html_escape(std::string(1, block.keyid_hex16.at(i))) << "</td>\n";
      }
      else
      {
        throw std::runtime_error("internal error: unexpected keyid data_row_index");
      }
    }
    else
    {
      out << "\t\t<td class=\"data\" colspan=2>" << html_escape("0 x") << "</td>\n";
      for (int i = 0; i < 16; ++i)
        out << "\t\t<td class=\"data\">" << html_escape(std::string(1, block.keyid_hex16.at(i))) << "</td>\n";
    }
  }
  else if (block.data == "grid10")
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
      out << "\t\t<td class=\"data\">" << html_escape(std::string(1, ch)) << "</td>\n";
  }

  write_empty_span(out, block.margin_right);
}

bool find_block_at_row(RowGroup::Column const& col, int row_offset, int& out_block_row, int& out_block_index)
{
  int cursor = 0;
  for (int const idx : col.blocks)
  {
    Block const& b = get_block(idx);
    if (row_offset < cursor + b.height)
    {
      out_block_row = row_offset - cursor;
      out_block_index = idx;
      return true;
    }
    cursor += b.height;
  }
  return false;
}

void write_sheet_html(std::ostream& output_file, json const& j, std::string const& sheet_label)
{
  std::string const title_left = j.at("title").at("left").get<std::string>();
  std::string const title_right = j.at("title").at("right").get<std::string>();
  int const table_width = parse_int(j.at("table").at("width"), sheet_label + ".table.width");

  std::cout << sheet_label << ".title.left: " << title_left << "\n";
  std::cout << sheet_label << ".title.right: " << title_right << "\n";
  std::cout << sheet_label << ".table.width: " << table_width << "\n\n";

  json const& headers = j.at("data_headers");
  json const& data = j.at("data");
  json const& margins = j.at("margins");

  if (!headers.is_object())
    throw std::runtime_error(sheet_label + ".data_headers must be an object");
  if (!data.is_object())
    throw std::runtime_error(sheet_label + ".data must be an object");
  if (!margins.is_object())
    throw std::runtime_error(sheet_label + ".margins must be an object");

  std::vector<Block> blocks;
  BlocksScope const _blocks_scope(blocks);

  std::vector<RowGroup> groups;
  RowGroup current_group(table_width);

  for (auto const& [key, header_value] : headers.items())
  {
    if (!data.contains(key))
      throw std::runtime_error(sheet_label + ": data_headers key '" + key + "' is missing from data");
    if (!margins.contains(key))
      throw std::runtime_error(sheet_label + ": data_headers key '" + key + "' is missing from margins");

    std::string const header = header_value.get<std::string>();
    std::string const data_value = data.at(key).get<std::string>();
    json const& margin_obj = margins.at(key);

    if (!margin_obj.is_object())
      throw std::runtime_error(sheet_label + ".margins." + key + " must be an object");

    int const margin_left =
        margin_obj.contains("left") ? parse_int(margin_obj.at("left"), sheet_label + ".margins." + key + ".left") : 0;
    int const margin_right =
        margin_obj.contains("right") ? parse_int(margin_obj.at("right"), sheet_label + ".margins." + key + ".right") : 0;

    int content_width = (key == "keyid") ? 18 : ((key == "keyid3") ? 10 : data_width(data_value));
    int height = (key == "keyid") ? 2 : ((key == "keyid3") ? 3 : data_height(data_value));
    std::string keyid_hex16;
    if (key == "keyid" || key == "keyid3")
      keyid_hex16 = parse_keyid_hex16(data_value);

    int width = content_width + margin_left + margin_right;

    if (width > table_width)
      throw std::runtime_error(sheet_label + ": block '" + key + "' has width " + std::to_string(width) + " > table width " +
                               std::to_string(table_width));

    Block block;
    block.key = key;
    block.header = header;
    block.data = data_value;
    block.keyid_hex16 = keyid_hex16;
    block.width = width;
    block.content_width = content_width;
    block.height = height;
    block.margin_left = margin_left;
    block.margin_right = margin_right;
    block.keyid_compact = key == "keyid3";

    blocks.push_back(block);
    int const block_index = static_cast<int>(blocks.size() - 1);

    auto try_compact_last_keyid_to_fit = [&](int new_block_index) -> bool {
      if (current_group.empty())
        return false;
      if (!current_group.last_column_has_single_block())
        return false;

      int const keyid_index = current_group.last_block_index();
      Block const& keyid = get_block(keyid_index);
      if (keyid.key != "keyid" || keyid.keyid_compact)
        return false;
      if (current_group.last_column().width != keyid.width)
        return false;

      Block const saved = keyid;
      Block& keyid_mut = get_block_mut(keyid_index);

      int const shrink = 8;
      if (keyid_mut.content_width < shrink + 2)
        return false;
      keyid_mut.keyid_compact = true;
      keyid_mut.content_width -= shrink;
      keyid_mut.width -= shrink;
      keyid_mut.height = 3;

      RowGroup rebuilt(table_width);
      for (int const idx : current_group.blocks_in_order())
      {
        if (!rebuilt.add(idx))
        {
          keyid_mut = saved;
          return false;
        }
      }
      if (!rebuilt.add(new_block_index))
      {
        keyid_mut = saved;
        return false;
      }

      current_group = std::move(rebuilt);
      return true;
    };

    if (current_group.add(block_index))
      continue;

    if (try_compact_last_keyid_to_fit(block_index))
      continue;

    if (!current_group.empty())
      groups.push_back(std::move(current_group));

    current_group = RowGroup(table_width);
    if (!current_group.add(block_index))
      throw std::runtime_error("internal error: failed to start new RowGroup");
  }

  if (!current_group.empty())
    groups.push_back(std::move(current_group));

  int group_top = 0;
  for (RowGroup const& group : groups)
  {
    int col_left = 0;
    for (auto const& col : group.columns())
    {
      int col_top = group_top;
      for (int const idx : col.blocks)
      {
        Block const& b = get_block(idx);
        std::cout << b.key << ": header='" << b.header << "' data='" << b.data << "' top=" << col_top << " left=" << col_left << " width=" << b.width
                  << " height=" << b.height;
        if (b.key == "keyid")
          std::cout << " compact=" << (b.keyid_compact ? 1 : 0);
        std::cout << "\n";
        col_top += b.height;
      }
      col_left += col.width;
    }
    group_top += group.height();
  }

  output_file << "<div class=\"sheet\">\n";
  output_file << "<h1 class=\"title\">\n";
  output_file << "  <span>" << html_escape(title_left) << "</span>\n";
  output_file << "  <span>" << html_escape(title_right) << "</span>\n";
  output_file << "</h1>\n";

  output_file << "<table cellspacing=\"0\" border=\"0\">\n";
  output_file << "\t<colgroup span=\"" << table_width << "\" width=\"25\"></colgroup>\n";

  for (RowGroup const& group : groups)
  {
    for (int row_offset = 0; row_offset < group.height(); ++row_offset)
    {
#if 0
      bool any_header = false;
      for (auto const& col : group.columns())
      {
        int block_row = 0;
        int block_index = -1;
        if (find_block_at_row(col, row_offset, block_row, block_index))
        {
          if (block_row == 0)
          {
            any_header = true;
            break;
          }
        }
      }

      if (any_header)
        output_file << "\t<tr class=\"header\">\n";
      else
#endif
        output_file << "\t<tr>\n";

      int used_width = 0;
      for (auto const& col : group.columns())
      {
        int block_row = 0;
        int block_index = -1;
        if (find_block_at_row(col, row_offset, block_row, block_index))
        {
          if (block_row == 0)
            write_block_header_row(output_file, get_block(block_index));
          else
            write_block_data_row(output_file, get_block(block_index), block_row - 1);

          write_empty_span(output_file, col.width - get_block(block_index).width);
        }
        else
        {
          write_empty_span(output_file, col.width);
        }
        used_width += col.width;
      }

      write_empty_span(output_file, table_width - used_width);
      output_file << "\t</tr>\n";
    }
  }

  output_file << "</table>\n";
  output_file << "</div>\n";
}

} // namespace

int main(int argc, char* argv[])
{
  if (argc != 2)
  {
    std::cerr << "Usage: " << argv[0] << " <basename>\n";
    std::cerr << "  Input is read from <basename>.json\n";
    std::cerr << "  Output will be written to <basename>.html\n";
    std::cerr << "  The input JSON may be a single object or an array of objects.\n";
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
    json const j = json::parse(input_file);

    json sheets = json::array();
    if (j.is_array())
      sheets = j;
    else if (j.is_object())
      sheets.push_back(j);
    else
      throw std::runtime_error("top-level JSON must be an object or array of objects");

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
)";

    for (std::size_t i = 0; i < sheets.size(); ++i)
    {
      json const& sheet_j = sheets.at(i);
      if (!sheet_j.is_object())
        throw std::runtime_error("top-level array element " + std::to_string(i) + " must be an object");

      std::string const label = (sheets.size() == 1) ? "sheet" : ("sheet[" + std::to_string(i) + "]");
      write_sheet_html(output_file, sheet_j, label);
    }

    output_file << "</body>\n</html>\n";
    std::cout << "\nWrote " << output_file_path << "\n";
  }
  catch (std::exception const& e)
  {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}
