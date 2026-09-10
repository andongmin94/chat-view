from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, content: str) -> None:
    (ROOT / path).write_text(content, encoding="utf-8", newline="\n")


def replace_once(path: str, old: str, new: str) -> None:
    content = read(path)
    count = content.count(old)
    if count != 1:
        raise RuntimeError(
            f"Expected exactly one anchor in {path}, found {count}: {old[:120]!r}"
        )
    write(path, content.replace(old, new, 1))


config_path = "src/common/chat-config.cpp"
config = read(config_path)

old_query = r'''        const std::size_t separator = extra.find(L'&');
        const std::wstring_view pair = extra.substr(0U, separator);
        const std::size_t equals = pair.find(L'=');
        if (equals != std::wstring_view::npos &&
            pair.substr(0U, equals) == key) {
'''
new_query = r'''        const std::size_t separator = extra.find(L'&');
        const std::wstring_view pair = extra.substr(0U, separator);
        if (pair == key) {
            return std::nullopt;
        }

        const std::size_t equals = pair.find(L'=');
        if (equals != std::wstring_view::npos &&
            pair.substr(0U, equals) == key) {
'''
if config.count(old_query) != 1:
    raise RuntimeError("Could not locate query parsing loop")
config = config.replace(old_query, new_query, 1)

reserved_start = config.find(
    "bool equals_ascii_case_insensitive(\n"
)
reserved_end = config.find(
    "\nstd::optional<std::wstring_view> soop_channel_from_player_path(\n",
    reserved_start,
)
if reserved_start < 0 or reserved_end < 0:
    raise RuntimeError("Could not locate speculative SOOP reserved-route helpers")
config = config[:reserved_start] + config[reserved_end + 1 :]

old_player_check = r'''    if (!is_ascii_identifier(
            channel_id, 1U, kMaximumSoopChannelIdLength) ||
        is_reserved_soop_channel_id(channel_id)) {
        return std::nullopt;
    }
'''
new_player_check = r'''    if (!is_ascii_identifier(
            channel_id, 1U, kMaximumSoopChannelIdLength)) {
        return std::nullopt;
    }
'''
if config.count(old_player_check) != 1:
    raise RuntimeError("Could not locate SOOP player channel validation")
config = config.replace(old_player_check, new_player_check, 1)

old_station_check = r'''        if (channel_id.has_value() &&
            (!is_ascii_identifier(
                 *channel_id, 1U, kMaximumSoopChannelIdLength) ||
             is_reserved_soop_channel_id(*channel_id))) {
            channel_id.reset();
        }
'''
new_station_check = r'''        if (channel_id.has_value() &&
            !is_ascii_identifier(
                *channel_id, 1U, kMaximumSoopChannelIdLength)) {
            channel_id.reset();
        }
'''
if config.count(old_station_check) != 1:
    raise RuntimeError("Could not locate SOOP station channel validation")
config = config.replace(old_station_check, new_station_check, 1)
write(config_path, config)


test_path = "tests/chat-config-test.cpp"
test = read(test_path)

normalization_anchor = r'''        !expect_normalized(
            canonical_soop,
            canonical_soop,
            L"SOOP chat URL normalization failed") ||
'''
normalization_replacement = r'''        !expect_normalized(
            canonical_soop,
            canonical_soop,
            L"SOOP chat URL normalization failed") ||
        !expect_normalized(
            L"https://play.sooplive.com/features",
            L"https://play.sooplive.com/features?vtype=chat",
            L"Valid SOOP channel named like a site route was rejected") ||
'''
if test.count(normalization_anchor) != 1:
    raise RuntimeError("Could not locate SOOP normalization tests")
test = test.replace(normalization_anchor, normalization_replacement, 1)

reserved_rejection = r'''        !expect_rejected(
            L"https://play.sooplive.com/features",
            L"Reserved SOOP player path was accepted as a channel") ||
'''
if test.count(reserved_rejection) != 1:
    raise RuntimeError("Could not locate speculative SOOP reserved-route test")
test = test.replace(reserved_rejection, "", 1)

duplicate_anchor = r'''        !expect_rejected(
            L"https://www.youtube.com/watch?v=first&v=second",
            L"Ambiguous duplicate YouTube video IDs were accepted") ||
'''
duplicate_replacement = duplicate_anchor + r'''        !expect_rejected(
            L"https://www.youtube.com/watch?v&v=dQw4w9WgXcQ",
            L"Bare duplicate YouTube video key was accepted") ||
'''
if test.count(duplicate_anchor) != 1:
    raise RuntimeError("Could not locate duplicate query-key test")
test = test.replace(duplicate_anchor, duplicate_replacement, 1)
write(test_path, test)

replace_once(
    "CMakeLists.txt",
    "project(chat-view-obs VERSION 0.2.6 LANGUAGES CXX)",
    "project(chat-view-obs VERSION 0.2.7 LANGUAGES CXX)",
)

replace_once(
    "docs/architecture.md",
    "an ASCII SOOP channel ID with an optional numeric broadcast route",
    "an ASCII SOOP channel ID with an optional numeric broadcast route",
)

print("SOOP route correction applied successfully.")
