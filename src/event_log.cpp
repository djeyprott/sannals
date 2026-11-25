#include "event_log.h"

void EventLog::add(Event e) {
  events_.push_back(std::move(e));
  if (listener_) listener_(events_.back());
}

const char* entityPath(EntityKind kind) {
  switch (kind) {
    case EntityKind::System: return "system";
    case EntityKind::Body: return "body";
    case EntityKind::Species: return "species";
    case EntityKind::Empire: return "empire";
    case EntityKind::Colony: return "colony";
  }
  return "unknown";
}

std::vector<EventSegment> segmentEvent(const Event& e, const NameResolver& nameOf) {
  std::vector<EventSegment> out;
  std::string plain;
  auto flush = [&] {
    if (!plain.empty()) out.push_back({std::move(plain), -1});
    plain.clear();
  };
  for (std::size_t i = 0; i < e.text.size(); ++i) {
    if (e.text[i] == '{') {
      const std::size_t close = e.text.find('}', i);
      if (close != std::string::npos) {
        const int index = std::stoi(e.text.substr(i + 1, close - i - 1));
        if (index >= 0 && index < static_cast<int>(e.refs.size())) {
          flush();
          out.push_back({nameOf(e.refs[static_cast<std::size_t>(index)]), index});
          i = close;
          continue;
        }
      }
    }
    plain += e.text[i];
  }
  flush();
  return out;
}

std::string renderEvent(const Event& e, const NameResolver& nameOf, LinkStyle style) {
  std::string out;
  for (const EventSegment& seg : segmentEvent(e, nameOf)) {
    if (seg.ref < 0 || style == LinkStyle::Plain) {
      out += seg.text;
    } else {
      const EntityRef& ref = e.refs[static_cast<std::size_t>(seg.ref)];
      out += "[" + seg.text + "](" + entityPath(ref.kind) + "/" + std::to_string(ref.id) + ")";
    }
  }
  return out;
}
