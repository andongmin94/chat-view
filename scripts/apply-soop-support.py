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

old_constants = (
    "constexpr std::size_t kChzzkChannelIdLength = 32U;\n"
    "constexpr std::size_t kMaximumYouTubeVideoIdLength = 64U;\n"
)
new_constants = (
    "constexpr std::size_t kChzzkChannelIdLength = 32U;\n"
    "constexpr std::size_t kMaximumSoopChannelIdLength = 64U;\n"
    "constexpr std::size_t kMaximumSoopBroadcastIdLength = 20U;\n"
    "constexpr std::size_t kMaximumYouTubeVideoIdLength = 64U;\n"
)
if config.count(old_constants) != 1:
    raise RuntimeError("Could not locate provider length constants")
config = config.replace(old_constants, new_constants, 1)

query_start = config.find(
    "std::optional<std::wstring_view> query_value(\n"
)
query_end = config.find(
    "\nstd::optional<std::wstring_view> path_segment_after(\n",
    query_start,
)
if query_start < 0 or query_end < 0:
    raise RuntimeError("Could not locate query_value")
query_value = r'''std::optional<std::wstring_view> query_value(
    std::wstring_view extra, std::wstring_view key) noexcept
{
    extra = query_without_fragment(extra);
    if (extra.empty() || extra.front() != L'?') {
        return std::nullopt;
    }

    std::optional<std::wstring_view> result;
    extra.remove_prefix(1U);
    while (!extra.empty()) {
        const std::size_t separator = extra.find(L'&');
        const std::wstring_view pair = extra.substr(0U, separator);
        const std::size_t equals = pair.find(L'=');
        if (equals != std::wstring_view::npos &&
            pair.substr(0U, equals) == key) {
            if (equals + 1U >= pair.size() || result.has_value()) {
                return std::nullopt;
            }
            result = pair.substr(equals + 1U);
        }

        if (separator == std::wstring_view::npos) {
            break;
        }
        extra.remove_prefix(separator + 1U);
    }
    return result;
}
'''
config = config[:query_start] + query_value + config[query_end:]

soop_helpers_anchor = "bool is_chzzk_channel_id(std::wstring_view value) noexcept\n"
soop_helpers_index = config.find(soop_helpers_anchor)
if soop_helpers_index < 0:
    raise RuntimeError("Could not locate CHZZK identifier helper")
soop_helpers = r'''bool is_ascii_digits(
    std::wstring_view value,
    std::size_t minimum,
    std::size_t maximum) noexcept
{
    if (value.size() < minimum || value.size() > maximum) {
        return false;
    }

    for (const wchar_t character : value) {
        if (character < L'0' || character > L'9') {
            return false;
        }
    }
    return true;
}

bool equals_ascii_case_insensitive(
    std::wstring_view left, std::wstring_view right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t index = 0U; index < left.size(); ++index) {
        const auto lowercase = [](wchar_t character) noexcept {
            return character >= L'A' && character <= L'Z'
                       ? static_cast<wchar_t>(
                             character - L'A' + L'a')
                       : character;
        };
        if (lowercase(left[index]) != lowercase(right[index])) {
            return false;
        }
    }
    return true;
}

bool is_reserved_soop_channel_id(std::wstring_view value) noexcept
{
    constexpr std::array<std::wstring_view, 5U> reserved{
        L"features",
        L"games",
        L"guide",
        L"guidelines",
        L"manager",
    };
    return std::any_of(
        reserved.begin(),
        reserved.end(),
        [value](std::wstring_view candidate) {
            return equals_ascii_case_insensitive(value, candidate);
        });
}

std::optional<std::wstring_view> soop_channel_from_player_path(
    std::wstring_view path) noexcept
{
    if (path.size() <= 1U || path.front() != L'/') {
        return std::nullopt;
    }

    path.remove_prefix(1U);
    if (!path.empty() && path.back() == L'/') {
        path.remove_suffix(1U);
    }
    if (path.empty() || path.back() == L'/') {
        return std::nullopt;
    }

    const std::size_t separator = path.find(L'/');
    const std::wstring_view channel_id = path.substr(0U, separator);
    if (!is_ascii_identifier(
            channel_id, 1U, kMaximumSoopChannelIdLength) ||
        is_reserved_soop_channel_id(channel_id)) {
        return std::nullopt;
    }

    if (separator != std::wstring_view::npos) {
        const std::wstring_view broadcast_id =
            path.substr(separator + 1U);
        if (broadcast_id.empty() ||
            broadcast_id.find(L'/') != std::wstring_view::npos ||
            (broadcast_id != L"null" &&
             !is_ascii_digits(
                 broadcast_id,
                 1U,
                 kMaximumSoopBroadcastIdLength))) {
            return std::nullopt;
        }
    }

    return channel_id;
}

'''
config = config[:soop_helpers_index] + soop_helpers + config[soop_helpers_index:]

host_anchor = r'''bool is_youtube_host(const std::wstring &host) noexcept
{
    return is_host(host, L"youtube.com") ||
           is_host(host, L"www.youtube.com") ||
           is_host(host, L"m.youtube.com");
}
'''
if config.count(host_anchor) != 1:
    raise RuntimeError("Could not locate YouTube host helper")
host_replacement = host_anchor + r'''

bool is_soop_station_host(const std::wstring &host) noexcept
{
    return is_host(host, L"sooplive.com") ||
           is_host(host, L"www.sooplive.com");
}

bool is_soop_player_host(const std::wstring &host) noexcept
{
    return is_host(host, L"play.sooplive.com");
}
'''
config = config.replace(host_anchor, host_replacement, 1)

canonical_anchor = (
    "std::wstring canonical_chzzk_chat_url(std::wstring_view channel_id)\n"
)
canonical_index = config.find(canonical_anchor)
if canonical_index < 0:
    raise RuntimeError("Could not locate canonical CHZZK URL helper")
soop_normalizer = r'''std::wstring canonical_soop_chat_url(std::wstring_view channel_id)
{
    std::wstring result = L"https://play.sooplive.com/";
    result.append(channel_id);
    result.append(L"?vtype=chat");
    return result;
}

std::wstring normalize_soop_url(const ParsedUrl &parsed)
{
    std::optional<std::wstring_view> channel_id;
    if (is_soop_player_host(parsed.host)) {
        channel_id = soop_channel_from_player_path(parsed.path);
    } else if (is_soop_station_host(parsed.host)) {
        channel_id = path_segment_after(parsed.path, L"/station/");
        if (channel_id.has_value() &&
            (!is_ascii_identifier(
                 *channel_id, 1U, kMaximumSoopChannelIdLength) ||
             is_reserved_soop_channel_id(*channel_id))) {
            channel_id.reset();
        }
    }

    return channel_id.has_value()
               ? canonical_soop_chat_url(*channel_id)
               : std::wstring{};
}

'''
config = config[:canonical_index] + soop_normalizer + config[canonical_index:]

normalize_anchor = r'''        if (std::wstring normalized = normalize_chzzk_url(*parsed);
            !normalized.empty()) {
            return normalized;
        }
        return normalize_youtube_url(*parsed);
'''
normalize_replacement = r'''        if (std::wstring normalized = normalize_chzzk_url(*parsed);
            !normalized.empty()) {
            return normalized;
        }
        if (std::wstring normalized = normalize_soop_url(*parsed);
            !normalized.empty()) {
            return normalized;
        }
        return normalize_youtube_url(*parsed);
'''
if config.count(normalize_anchor) != 1:
    raise RuntimeError("Could not locate provider normalization dispatch")
config = config.replace(normalize_anchor, normalize_replacement, 1)

document_anchor = r'''        if (is_youtube_host(parsed->host) &&
            parsed->path == L"/live_chat") {
'''
if config.count(document_anchor) != 1:
    raise RuntimeError("Could not locate YouTube document validation")
document_replacement = r'''        if (is_soop_player_host(parsed->host)) {
            return soop_channel_from_player_path(parsed->path).has_value();
        }

        if (is_youtube_host(parsed->host) &&
            parsed->path == L"/live_chat") {
'''
config = config.replace(document_anchor, document_replacement, 1)
write(config_path, config)


test_path = "tests/chat-config-test.cpp"
test = read(test_path)

constants_anchor = (
    'constexpr wchar_t kOtherChzzkChannelId[] =\n'
    '    L"0123456789abcdef0123456789abcdef";\n'
    'constexpr wchar_t kYouTubeVideoId[] = L"dQw4w9WgXcQ";\n'
)
constants_replacement = (
    'constexpr wchar_t kOtherChzzkChannelId[] =\n'
    '    L"0123456789abcdef0123456789abcdef";\n'
    'constexpr wchar_t kSoopChannelId[] = L"index0959";\n'
    'constexpr wchar_t kOtherSoopChannelId[] = L"ksh7637";\n'
    'constexpr wchar_t kSoopBroadcastId[] = L"288167758";\n'
    'constexpr wchar_t kYouTubeVideoId[] = L"dQw4w9WgXcQ";\n'
)
if test.count(constants_anchor) != 1:
    raise RuntimeError("Could not locate test provider constants")
test = test.replace(constants_anchor, constants_replacement, 1)

canonical_test_anchor = r'''    const std::wstring other_chzzk =
        std::wstring(L"https://chzzk.naver.com/chat/") +
        kOtherChzzkChannelId;
    const std::wstring canonical_youtube =
'''
canonical_test_replacement = r'''    const std::wstring other_chzzk =
        std::wstring(L"https://chzzk.naver.com/chat/") +
        kOtherChzzkChannelId;
    const std::wstring canonical_soop =
        std::wstring(L"https://play.sooplive.com/") +
        kSoopChannelId + L"?vtype=chat";
    const std::wstring other_soop =
        std::wstring(L"https://play.sooplive.com/") +
        kOtherSoopChannelId + L"?vtype=chat";
    const std::wstring canonical_youtube =
'''
if test.count(canonical_test_anchor) != 1:
    raise RuntimeError("Could not locate canonical test URLs")
test = test.replace(canonical_test_anchor, canonical_test_replacement, 1)

normalization_test_anchor = r'''        !expect_normalized(
            std::wstring(L"https://www.youtube.com/watch?v=") +
'''
normalization_test_replacement = r'''        !expect_normalized(
            std::wstring(L"https://www.sooplive.com/station/") +
                kSoopChannelId,
            canonical_soop,
            L"SOOP station URL normalization failed") ||
        !expect_normalized(
            std::wstring(L"https://sooplive.com/station/") +
                kSoopChannelId + L"/#ignored",
            canonical_soop,
            L"Bare SOOP station host normalization failed") ||
        !expect_normalized(
            std::wstring(L"https://PLAY.SOOPLIVE.COM/") +
                kSoopChannelId + L"/" + kSoopBroadcastId +
                L"?quality=original#ignored",
            canonical_soop,
            L"SOOP live player URL normalization failed") ||
        !expect_normalized(
            std::wstring(L"https://play.sooplive.com/") +
                kSoopChannelId + L"/null?vtype=chat",
            canonical_soop,
            L"SOOP offline player URL normalization failed") ||
        !expect_normalized(
            canonical_soop,
            canonical_soop,
            L"SOOP chat URL normalization failed") ||
        !expect_normalized(
            std::wstring(L"https://www.youtube.com/watch?v=") +
'''
if test.count(normalization_test_anchor) != 1:
    raise RuntimeError("Could not locate normalization test insertion point")
test = test.replace(
    normalization_test_anchor, normalization_test_replacement, 1
)

rejection_test_anchor = r'''        !expect_rejected(
            L"https://www.youtube.com/live_chat",
            L"YouTube live-chat URL without a video ID was accepted") ||
'''
rejection_test_replacement = r'''        !expect_rejected(
            L"https://play.sooplive.com/features",
            L"Reserved SOOP player path was accepted as a channel") ||
        !expect_rejected(
            L"https://play.sooplive.com/index0959/not-a-broadcast",
            L"Malformed SOOP broadcast ID was accepted") ||
        !expect_rejected(
            L"https://www.sooplive.com/station/index0959/vod",
            L"Nested SOOP station path was accepted") ||
        !expect_rejected(
            L"https://play.sooplive.com/index.0959",
            L"Malformed SOOP channel ID was accepted") ||
        !expect_rejected(
            L"https://www.youtube.com/live_chat",
            L"YouTube live-chat URL without a video ID was accepted") ||
        !expect_rejected(
            L"https://www.youtube.com/watch?v=first&v=second",
            L"Ambiguous duplicate YouTube video IDs were accepted") ||
'''
if test.count(rejection_test_anchor) != 1:
    raise RuntimeError("Could not locate rejection test insertion point")
test = test.replace(rejection_test_anchor, rejection_test_replacement, 1)

document_test_anchor = r'''        !expect_document_allowed(
            canonical_youtube + L"&embed_domain=localhost",
'''
document_test_replacement = r'''        !expect_document_allowed(
            canonical_soop + L"&theme=dark",
            L"Canonical SOOP chat player was not accepted as a document") ||
        !expect_document_allowed(
            std::wstring(L"https://play.sooplive.com/") +
                kSoopChannelId + L"/" + kSoopBroadcastId,
            L"SOOP same-channel redirect was not accepted as a document") ||
        !expect_document_allowed(
            canonical_youtube + L"&embed_domain=localhost",
'''
if test.count(document_test_anchor) != 1:
    raise RuntimeError("Could not locate document test insertion point")
test = test.replace(document_test_anchor, document_test_replacement, 1)

station_reject_anchor = r'''        !expect_document_rejected(
            std::wstring(L"https://www.youtube.com/watch?v=") +
'''
station_reject_replacement = r'''        !expect_document_rejected(
            std::wstring(L"https://www.sooplive.com/station/") +
                kSoopChannelId,
            L"SOOP station page was accepted as a chat document") ||
        !expect_document_rejected(
            std::wstring(L"https://www.youtube.com/watch?v=") +
'''
if test.count(station_reject_anchor) != 1:
    raise RuntimeError("Could not locate document rejection insertion point")
test = test.replace(station_reject_anchor, station_reject_replacement, 1)

matching_anchor = r'''        !expect_matching_document(
            canonical_youtube + L"&embed_domain=localhost",
'''
matching_replacement = r'''        !expect_matching_document(
            std::wstring(L"https://play.sooplive.com/") +
                kSoopChannelId + L"/" + kSoopBroadcastId +
                L"?vtype=chat&theme=dark",
            canonical_soop,
            L"Matching SOOP live redirect was rejected") ||
        !expect_matching_document(
            canonical_youtube + L"&embed_domain=localhost",
'''
if test.count(matching_anchor) != 1:
    raise RuntimeError("Could not locate matching test insertion point")
test = test.replace(matching_anchor, matching_replacement, 1)

nonmatching_anchor = r'''        !expect_nonmatching_document(
            other_youtube,
            canonical_youtube,
            L"Different YouTube video was accepted") ||
'''
nonmatching_replacement = r'''        !expect_nonmatching_document(
            other_soop,
            canonical_soop,
            L"Different SOOP channel was accepted") ||
        !expect_nonmatching_document(
            other_youtube,
            canonical_youtube,
            L"Different YouTube video was accepted") ||
'''
if test.count(nonmatching_anchor) != 1:
    raise RuntimeError("Could not locate nonmatching test insertion point")
test = test.replace(nonmatching_anchor, nonmatching_replacement, 1)

persistence_start = test.find(
    "    const chatview::ChatConfig input{\n"
)
persistence_end = test.find(
    "\n    const std::wstring user_data_folder =",
    persistence_start,
)
if persistence_start < 0 or persistence_end < 0:
    raise RuntimeError("Could not locate persistence test")
persistence = r'''    const chatview::ChatConfig input{
        std::wstring(L"https://play.sooplive.com/") +
        kSoopChannelId + L"/" + kSoopBroadcastId};
    if (!chatview::save_chat_config(input)) {
        return fail(L"Failed to save a normal SOOP broadcast URL");
    }

    chatview::ChatConfig loaded;
    if (!chatview::load_chat_config(loaded) ||
        loaded.url != canonical_soop) {
        return fail(
            L"Saved SOOP URL was not persisted in canonical chat form");
    }
'''
test = test[:persistence_start] + persistence + test[persistence_end:]
write(test_path, test)

replace_once(
    "src/config/main.cpp",
    '            L"Paste a Weflab page, CHZZK channel/live/chat URL, or YouTube watch/live/chat URL.\\nChatView converts normal CHZZK and YouTube broadcast links automatically. Leave empty to disable chat.",\n',
    '            L"Paste a Weflab page or a CHZZK, SOOP, or YouTube broadcast/chat URL.\\nChatView converts normal broadcast links automatically. Leave empty to disable chat.",\n',
)
replace_once(
    "src/hud/webview-host.cpp",
    "<p>Paste a Weflab page, CHZZK broadcast, or YouTube live URL, then save.</p>",
    "<p>Paste a Weflab page or a CHZZK, SOOP, or YouTube broadcast/chat URL, then save.</p>",
)

replace_once(
    "CMakeLists.txt",
    "project(chat-view-obs VERSION 0.2.5 LANGUAGES CXX)",
    "project(chat-view-obs VERSION 0.2.6 LANGUAGES CXX)",
)

replace_once(
    "README.md",
    "- Weflab, CHZZK, and YouTube chat pages;\n"
    "- automatic conversion of ordinary CHZZK and YouTube broadcast URLs;\n",
    "- Weflab, CHZZK, SOOP, and YouTube chat pages;\n"
    "- automatic conversion of ordinary CHZZK, SOOP, and YouTube broadcast URLs;\n",
)
replace_once(
    "README.md",
    "For CHZZK and YouTube, paste the normal broadcast URL that is already in the browser address bar:\n\n"
    "```text\n"
    "https://chzzk.naver.com/live/<channel-id>\n"
    "https://www.youtube.com/watch?v=<video-id>\n"
    "https://youtu.be/<video-id>\n"
    "```\n",
    "For CHZZK, SOOP, and YouTube, paste the normal channel or broadcast URL that is already in the browser address bar:\n\n"
    "```text\n"
    "https://chzzk.naver.com/live/<channel-id>\n"
    "https://www.sooplive.com/station/<channel-id>\n"
    "https://play.sooplive.com/<channel-id>/<broadcast-number>\n"
    "https://www.youtube.com/watch?v=<video-id>\n"
    "https://youtu.be/<video-id>\n"
    "```\n",
)
replace_once(
    "README.md",
    "https://chzzk.naver.com/chat/<channel-id>\n"
    "https://www.youtube.com/live_chat?is_popout=1&v=<video-id>\n",
    "https://chzzk.naver.com/chat/<channel-id>\n"
    "https://play.sooplive.com/<channel-id>?vtype=chat\n"
    "https://www.youtube.com/live_chat?is_popout=1&v=<video-id>\n",
)
replace_once(
    "README.md",
    "The URL normalizer requires HTTPS, the default HTTPS port, no embedded credentials, a supported host and path, a valid CHZZK channel ID, and a non-empty YouTube video ID. It stores only the normalized chat URL. New top-level WebView navigation outside the allowlist is cancelled.\n",
    "The URL normalizer requires HTTPS, the default HTTPS port, no embedded credentials, a supported host and path, a valid CHZZK channel ID, a valid SOOP channel path, and a non-empty YouTube video ID. SOOP broadcast-number routes are reduced to a channel-scoped `vtype=chat` URL so a live redirect remains bound to the configured streamer. It stores only the normalized chat URL. New top-level WebView navigation outside the allowlist is cancelled.\n",
)
replace_once(
    "README.md",
    "Direct SOOP support, dual-PC pairing, first-party multi-platform aggregation, account/backend services, and the creator advertising system remain later layers.",
    "Dual-PC pairing, first-party multi-platform aggregation, account/backend services, and the creator advertising system remain later layers.",
)

replace_once(
    "docs/architecture.md",
    "The settings application accepts either direct chat pages or normal CHZZK and YouTube broadcast links. The configuration layer converts supported broadcast links into one canonical chat URL before persistence:\n",
    "The settings application accepts either direct chat pages or normal CHZZK, SOOP, and YouTube channel/broadcast links. The configuration layer converts supported links into one canonical chat URL before persistence:\n",
)
replace_once(
    "docs/architecture.md",
    "CHZZK /live/<channel-id> or /<channel-id>\n"
    "    → https://chzzk.naver.com/chat/<channel-id>\n\n"
    "YouTube /watch?v=<video-id>, /live/<video-id>, youtu.be/<video-id>\n",
    "CHZZK /live/<channel-id> or /<channel-id>\n"
    "    → https://chzzk.naver.com/chat/<channel-id>\n\n"
    "SOOP /station/<channel-id> or play/<channel-id>[/<broadcast-number>]\n"
    "    → https://play.sooplive.com/<channel-id>?vtype=chat\n\n"
    "YouTube /watch?v=<video-id>, /live/<video-id>, youtu.be/<video-id>\n",
)
replace_once(
    "docs/architecture.md",
    "Weflab `/page/...` URLs remain direct. The normalizer requires HTTPS, the default HTTPS port, no URL credentials, supported paths, a 32-character hexadecimal CHZZK channel ID, and an ASCII YouTube video identifier. Unsupported or ambiguous links are rejected instead of being guessed.\n",
    "Weflab `/page/...` URLs remain direct. The normalizer requires HTTPS, the default HTTPS port, no URL credentials, supported paths, a 32-character hexadecimal CHZZK channel ID, an ASCII SOOP channel ID with an optional numeric broadcast route, and an ASCII YouTube video identifier. SOOP canonicalization intentionally binds to the streamer channel rather than one transient broadcast number. Unsupported or ambiguous links are rejected instead of being guessed.\n",
)

print("Direct SOOP URL support applied successfully.")
