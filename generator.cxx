#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <algorithm>
#include <stdexcept>
#include <nlohmann/json.hpp>

using json = nlohmann::ordered_json;

namespace {

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

} // namespace

int main(int argc, char* argv[])
{
  if (argc != 2)
  {
    std::cerr << "Usage: " << argv[0] << " <basename>\n";
    std::cerr << "  Input is read from <basename>.json\n";
    std::cerr << "  Output is printed to stdout\n";
    return 1;
  }

  std::string const basename = argv[1];
  std::string const input_filename = basename + ".json";

  namespace fs = std::filesystem;
  fs::path const input_file_path(input_filename);

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
        cursor_top += current_row_height;
        cursor_left = 0;
        current_row_height = 0;
      }

      int const block_left = cursor_left;
      int const block_top = cursor_top;
      cursor_left += width;
      current_row_height = std::max(current_row_height, height);

      std::cout << key << ": header='" << header << "' data='" << data_value << "' top=" << block_top << " left=" << block_left << " width=" << width
                << " height=" << height << "\n";
    }
  }
  catch (std::exception const& e)
  {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}
