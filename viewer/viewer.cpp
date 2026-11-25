#include "viewer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

#include "imgui.h"

namespace {

struct Speed {
  const char* label;
  double daysPerSecond;
};
const Speed kSpeeds[] = {
    {"1 day/s", 1}, {"1 week/s", 7}, {"1 month/s", 30}, {"3 months/s", 90}, {"1 year/s", 360}, {"5 years/s", 1800},
};
constexpr int kSpeedCount = static_cast<int>(std::size(kSpeeds));
constexpr int kMaxDaysPerFrame = 3600;  // keeps the UI responsive at top speed

const char* const kImportanceNames[] = {"Minor", "Notable", "Major"};

template <class T>
const T& at(const std::vector<T>& v, Id id) {
  return v[static_cast<std::size_t>(id)];
}

ImU32 empireColor(Id id, float alpha = 1.0f) {
  const float hue = std::fmod(static_cast<float>(id) * 0.618034f + 0.08f, 1.0f);
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(hue, 0.65f, 0.95f, r, g, b);
  return ImGui::GetColorU32(ImVec4(r, g, b, alpha));
}

ImU32 starColor(StarClass c) {
  switch (c) {
    case StarClass::O: return IM_COL32(155, 176, 255, 255);
    case StarClass::B: return IM_COL32(170, 191, 255, 255);
    case StarClass::A: return IM_COL32(202, 215, 255, 255);
    case StarClass::F: return IM_COL32(248, 247, 255, 255);
    case StarClass::G: return IM_COL32(255, 244, 214, 255);
    case StarClass::K: return IM_COL32(255, 210, 161, 255);
    case StarClass::M: return IM_COL32(255, 160, 120, 255);
  }
  return IM_COL32_WHITE;
}

const char* bodyKindName(BodyKind k) {
  switch (k) {
    case BodyKind::Rocky: return "Rocky";
    case BodyKind::GasGiant: return "Gas giant";
    case BodyKind::IceWorld: return "Ice world";
    case BodyKind::AsteroidBelt: return "Asteroids";
  }
  return "?";
}

bool sameRef(EntityRef a, EntityKind kind, Id id) { return a.kind == kind && a.id == id; }

}  // namespace

Viewer::Viewer(World& world, Sim& sim, std::string title)
    : w_(world), sim_(sim), title_(std::move(title)), resolve_([this](EntityRef r) { return w_.nameOf(r); }) {}

void Viewer::frame(float dt) {
  ImGuiIO& io = ImGui::GetIO();
  // Shortcuts work whenever the player isn't typing into a text field,
  // even while a panel has focus.
  if (!io.WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) paused_ = !paused_;
    for (int i = 0; i < kSpeedCount; ++i) {
      if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + i))) speedIndex_ = i;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F)) viewFitted_ = false;
  }

  advance(dt);
  recomputeOwnership();

  drawMap();
  drawControls();
  drawEventFeed();
  drawEmpires();
  drawDetails();
}

// ---- Clock -----------------------------------------------------------------

void Viewer::advance(float dt) {
  if (paused_) return;
  dayAccumulator_ += static_cast<double>(dt) * kSpeeds[speedIndex_].daysPerSecond;
  const int steps = std::min(static_cast<int>(dayAccumulator_), kMaxDaysPerFrame);
  dayAccumulator_ -= steps;
  if (dayAccumulator_ > kMaxDaysPerFrame) dayAccumulator_ = 0.0;  // drop backlog rather than stall
  for (int i = 0; i < steps; ++i) sim_.stepDay();
}

void Viewer::recomputeOwnership() {
  systemOwner_.assign(w_.systems.size(), kNone);
  for (const Colony& c : w_.colonies) {
    if (c.empire == kNone) continue;
    const Id sys = at(w_.bodies, c.body).system;
    Id& owner = systemOwner_[static_cast<std::size_t>(sys)];
    if (owner == kNone) owner = c.empire;
  }
}

double Viewer::empirePopulation(const Empire& e) const {
  double total = 0.0;
  for (Id c : e.colonies) total += at(w_.colonies, c).population;
  return total;
}

// ---- Map -------------------------------------------------------------------

void Viewer::fitView(float width, float height) {
  double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
  for (const StarSystem& s : w_.systems) {
    minX = std::min(minX, s.x);
    maxX = std::max(maxX, s.x);
    minY = std::min(minY, s.y);
    maxY = std::max(maxY, s.y);
  }
  camX_ = (minX + maxX) / 2;
  camY_ = (minY + maxY) / 2;
  scale_ = std::min(width / ((maxX - minX) * 1.1 + 1), height / ((maxY - minY) * 1.1 + 1));
  viewFitted_ = true;
}

void Viewer::drawMap() {
  ImGuiIO& io = ImGui::GetIO();
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const ImVec2 size = vp->Size;
  const ImVec2 center(vp->Pos.x + size.x * 0.5f, vp->Pos.y + size.y * 0.5f);
  if (!viewFitted_) fitView(size.x, size.y);

  auto toScreen = [&](double x, double y) {
    return ImVec2(center.x + static_cast<float>((x - camX_) * scale_), center.y + static_cast<float>((y - camY_) * scale_));
  };

  // Input: wheel zooms around the cursor, left-drag pans, left-click selects.
  Id hovered = kNone;
  if (!io.WantCaptureMouse) {
    const ImVec2 mouse = io.MousePos;
    if (io.MouseWheel != 0.0f) {
      const double wx = camX_ + (mouse.x - center.x) / scale_;
      const double wy = camY_ + (mouse.y - center.y) / scale_;
      scale_ = std::clamp(scale_ * std::pow(1.15, static_cast<double>(io.MouseWheel)), 0.02, 200.0);
      camX_ = wx - (mouse.x - center.x) / scale_;
      camY_ = wy - (mouse.y - center.y) / scale_;
    }
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      const ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
      camX_ -= d.x / scale_;
      camY_ -= d.y / scale_;
      ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
      mapDragged_ = true;
    }

    float best = 10.0f * 10.0f;  // pick radius in pixels, squared
    for (const StarSystem& s : w_.systems) {
      const ImVec2 p = toScreen(s.x, s.y);
      const float dx = p.x - mouse.x, dy = p.y - mouse.y;
      const float d2 = dx * dx + dy * dy;
      if (d2 < best) {
        best = d2;
        hovered = s.id;
      }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !mapDragged_ && hovered != kNone) {
      select({EntityKind::System, hovered});
    }
    if (hovered != kNone && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      const StarSystem& s = at(w_.systems, hovered);
      const Id owner = systemOwner_[static_cast<std::size_t>(hovered)];
      ImGui::BeginTooltip();
      ImGui::Text("%s  (%s-class, %zu bodies)", s.name.c_str(), starClassName(s.star), s.bodies.size());
      if (owner != kNone) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(empireColor(owner)), "%s", at(w_.empires, owner).name.c_str());
      ImGui::EndTooltip();
    }
  }
  if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) mapDragged_ = false;

  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  const float margin = 40.0f;
  auto onScreen = [&](ImVec2 p) {
    return p.x > vp->Pos.x - margin && p.y > vp->Pos.y - margin && p.x < vp->Pos.x + size.x + margin &&
           p.y < vp->Pos.y + size.y + margin;
  };

  if (showLanes_) {
    const ImU32 laneColor = IM_COL32(80, 95, 130, 110);
    for (const StarSystem& s : w_.systems) {
      const ImVec2 a = toScreen(s.x, s.y);
      for (Id other : s.lanes) {
        if (other < s.id) continue;  // draw each lane once
        const StarSystem& o = at(w_.systems, other);
        const ImVec2 b = toScreen(o.x, o.y);
        if (!onScreen(a) && !onScreen(b)) continue;
        dl->AddLine(a, b, laneColor, 1.0f);
      }
    }
  }

  const float starRadius = std::clamp(static_cast<float>(scale_) * 0.5f, 1.2f, 5.0f);
  for (const StarSystem& s : w_.systems) {
    const ImVec2 p = toScreen(s.x, s.y);
    if (!onScreen(p)) continue;
    const Id owner = systemOwner_[static_cast<std::size_t>(s.id)];
    if (owner != kNone) {
      dl->AddCircleFilled(p, starRadius + 3.0f, empireColor(owner, 0.35f), 12);
      dl->AddCircle(p, starRadius + 3.0f, empireColor(owner), 12, 1.5f);
    }
    dl->AddCircleFilled(p, starRadius, starColor(s.star), 8);
  }

  if (showShips_) {
    for (const ColonyShip& ship : w_.ships) {
      if (ship.leg + 1 >= ship.route.size()) continue;
      const StarSystem& a = at(w_.systems, ship.route[ship.leg]);
      const StarSystem& b = at(w_.systems, ship.route[ship.leg + 1]);
      const ImVec2 p = toScreen(a.x + (b.x - a.x) * ship.progress, a.y + (b.y - a.y) * ship.progress);
      if (!onScreen(p)) continue;
      dl->AddRectFilled(ImVec2(p.x - 2, p.y - 2), ImVec2(p.x + 2, p.y + 2), empireColor(ship.empire));
    }
  }

  if (showLabels_) {
    for (const StarSystem& s : w_.systems) {
      const bool owned = systemOwner_[static_cast<std::size_t>(s.id)] != kNone;
      if (!(scale_ > 6.0 || (owned && scale_ > 1.5))) continue;
      const ImVec2 p = toScreen(s.x, s.y);
      if (!onScreen(p)) continue;
      dl->AddText(ImVec2(p.x + starRadius + 4, p.y - 7), owned ? IM_COL32(230, 230, 240, 230) : IM_COL32(150, 155, 170, 160),
                  s.name.c_str());
    }
  }

  // Highlight the selected entity's system.
  if (selection_.valid()) {
    const Id sys = systemOf({selection_.kind, selection_.id});
    if (sys != kNone) {
      const StarSystem& s = at(w_.systems, sys);
      dl->AddCircle(toScreen(s.x, s.y), starRadius + 8.0f, IM_COL32_WHITE, 24, 2.0f);
    }
  }
  if (hovered != kNone) {
    const StarSystem& s = at(w_.systems, hovered);
    dl->AddCircle(toScreen(s.x, s.y), starRadius + 6.0f, IM_COL32(255, 255, 255, 120), 20, 1.0f);
  }
}

// ---- Panels ----------------------------------------------------------------

void Viewer::drawControls() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + 10, vp->Pos.y + 10), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
  ImGui::Begin("Time");
  ImGui::TextDisabled("%s", title_.c_str());
  ImGui::Text("Date  %s", formatDate(w_.day).c_str());
  ImGui::SameLine(200);
  ImGui::TextDisabled("Year %lld", static_cast<long long>(w_.day / kDaysPerYear));

  if (ImGui::Button(paused_ ? "  Play  " : " Pause ")) paused_ = !paused_;
  ImGui::SameLine();
  ImGui::SetNextItemWidth(-1);
  if (ImGui::BeginCombo("##speed", kSpeeds[speedIndex_].label)) {
    for (int i = 0; i < kSpeedCount; ++i) {
      if (ImGui::Selectable(kSpeeds[i].label, i == speedIndex_)) speedIndex_ = i;
    }
    ImGui::EndCombo();
  }
  ImGui::Checkbox("Lanes", &showLanes_);
  ImGui::SameLine();
  ImGui::Checkbox("Ships", &showShips_);
  ImGui::SameLine();
  ImGui::Checkbox("Names", &showLabels_);
  ImGui::SameLine();
  if (ImGui::SmallButton("Fit")) viewFitted_ = false;
  ImGui::TextDisabled("Space pause - 1-6 speed - F fit - drag pan - wheel zoom");
  ImGui::End();
}

void Viewer::eventRow(const Event& e, std::size_t uid) {
  ImGui::PushID(static_cast<int>(uid));
  ImGui::TextDisabled("%s", formatDate(e.day).c_str());
  ImGui::SameLine();
  const std::vector<EventSegment> segments = segmentEvent(e, resolve_);
  for (std::size_t i = 0; i < segments.size(); ++i) {
    if (i > 0) ImGui::SameLine(0.0f, 0.0f);
    const EventSegment& seg = segments[i];
    if (seg.ref < 0) {
      ImGui::TextUnformatted(seg.text.c_str());
    } else {
      ImGui::PushID(static_cast<int>(i));
      const EntityRef ref = e.refs[static_cast<std::size_t>(seg.ref)];
      if (ImGui::TextLink(seg.text.c_str())) select(ref);
      ImGui::PopID();
    }
  }
  ImGui::PopID();
}

void Viewer::drawEventFeed() {
  const auto& events = w_.log.events();
  for (; feedScanned_ < events.size(); ++feedScanned_) {
    if (static_cast<int>(events[feedScanned_].importance) >= feedMinImportance_) feed_.push_back(feedScanned_);
  }

  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + 10, vp->Pos.y + vp->Size.y - 270), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(760, 260), ImGuiCond_FirstUseEver);
  ImGui::Begin("Chronicle");
  ImGui::SetNextItemWidth(120);
  if (ImGui::Combo("Detail", &feedMinImportance_, kImportanceNames, 3)) {
    feed_.clear();
    feedScanned_ = 0;
  }
  ImGui::SameLine();
  ImGui::Checkbox("Follow newest", &feedFollow_);
  ImGui::SameLine();
  ImGui::TextDisabled("%zu shown / %zu recorded", feed_.size(), events.size());

  ImGui::BeginChild("feed", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(feed_.size()));
  while (clipper.Step()) {
    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
      const std::size_t index = feed_[static_cast<std::size_t>(row)];
      eventRow(events[index], index);
    }
  }
  if (feedFollow_) ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();
  ImGui::End();
}

void Viewer::drawEmpires() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - 430, vp->Pos.y + 10), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(420, 260), ImGuiCond_FirstUseEver);
  ImGui::Begin("Empires");

  std::vector<const Empire*> rows;
  for (const Empire& e : w_.empires) rows.push_back(&e);
  std::sort(rows.begin(), rows.end(),
            [this](const Empire* a, const Empire* b) { return empirePopulation(*a) > empirePopulation(*b); });

  const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                                ImGuiTableFlags_SizingStretchProp;
  if (ImGui::BeginTable("empires", 4, flags)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Empire", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("Worlds", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Pop (B)", ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn("Minerals", ImGuiTableColumnFlags_WidthStretch, 1.3f);
    ImGui::TableHeadersRow();
    for (const Empire* e : rows) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float h = ImGui::GetTextLineHeight();
      ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + 2), ImVec2(p.x + h - 4, p.y + h - 2), empireColor(e->id));
      ImGui::Dummy(ImVec2(h - 2, h));
      ImGui::SameLine();
      entityLink({EntityKind::Empire, e->id});
      ImGui::TableNextColumn();
      ImGui::Text("%zu", e->colonies.size());
      ImGui::TableNextColumn();
      ImGui::Text("%.1f", empirePopulation(*e) / 1000.0);
      ImGui::TableNextColumn();
      ImGui::Text("%.0f", e->minerals);
    }
    ImGui::EndTable();
  }
  ImGui::End();
}

void Viewer::drawDetails() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - 430, vp->Pos.y + 280), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(420, vp->Size.y - 290), ImGuiCond_FirstUseEver);
  ImGui::Begin("Details");
  if (!selection_.valid()) {
    ImGui::TextDisabled("Click a star or any name to see it here.");
    ImGui::End();
    return;
  }

  switch (selection_.kind) {
    case EntityKind::System: drawSystemDetails(at(w_.systems, selection_.id)); break;
    case EntityKind::Body: drawBodyDetails(at(w_.bodies, selection_.id)); break;
    case EntityKind::Species: drawSpeciesDetails(at(w_.species, selection_.id)); break;
    case EntityKind::Empire: drawEmpireDetails(at(w_.empires, selection_.id)); break;
    case EntityKind::Colony: drawBodyDetails(at(w_.bodies, at(w_.colonies, selection_.id).body)); break;
  }
  drawHistory();
  ImGui::End();
}

void Viewer::drawSystemDetails(const StarSystem& sys) {
  ImGui::SeparatorText(sys.name.c_str());
  ImGui::Text("%s-class star at (%.0f, %.0f) ly", starClassName(sys.star), sys.x, sys.y);
  if (ImGui::SmallButton("Center on map")) focusOn({EntityKind::System, sys.id});

  ImGui::TextDisabled("Jump lanes:");
  for (Id other : sys.lanes) {
    ImGui::SameLine();
    entityLink({EntityKind::System, other});
  }

  const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
  if (ImGui::BeginTable("bodies", 6, flags)) {
    ImGui::TableSetupColumn("Body");
    ImGui::TableSetupColumn("Type");
    ImGui::TableSetupColumn("Temp K");
    ImGui::TableSetupColumn("g");
    ImGui::TableSetupColumn("Minerals");
    ImGui::TableSetupColumn("Settled by");
    ImGui::TableHeadersRow();
    for (Id id : sys.bodies) {
      const Body& b = at(w_.bodies, id);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      entityLink({EntityKind::Body, b.id});
      ImGui::TableNextColumn();
      ImGui::Text("%s%s", bodyKindName(b.kind), b.atmosphere ? ", air" : "");
      ImGui::TableNextColumn();
      ImGui::Text("%.0f", b.temperatureK);
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", b.gravity);
      ImGui::TableNextColumn();
      ImGui::Text("%.0f", b.minerals);
      ImGui::TableNextColumn();
      if (b.colony != kNone) {
        const Colony& c = at(w_.colonies, b.colony);
        if (c.empire != kNone) entityLink({EntityKind::Empire, c.empire});
        else entityLink({EntityKind::Species, c.species});
      }
    }
    ImGui::EndTable();
  }
}

void Viewer::drawBodyDetails(const Body& b) {
  ImGui::SeparatorText(b.name.c_str());
  ImGui::Text("%s in", bodyKindName(b.kind));
  ImGui::SameLine();
  entityLink({EntityKind::System, b.system});
  ImGui::Text("Temperature %.0f K, gravity %.2f g, %s", b.temperatureK, b.gravity,
              b.atmosphere ? "breathable atmosphere" : "no usable atmosphere");
  ImGui::Text("Minerals remaining: %.0f", b.minerals);
  if (b.colony == kNone) {
    ImGui::TextDisabled("Unsettled");
    return;
  }
  const Colony& c = at(w_.colonies, b.colony);
  const Species& sp = at(w_.species, c.species);
  ImGui::SeparatorText("Colony");
  ImGui::Text("Population %.2f billion", c.population / 1000.0);
  ImGui::Text("Species");
  ImGui::SameLine();
  entityLink({EntityKind::Species, sp.id});
  ImGui::Text("Habitability for them: %.0f%%", habitability(sp, b) * 100.0);
  if (c.empire != kNone) {
    ImGui::Text("Part of");
    ImGui::SameLine();
    entityLink({EntityKind::Empire, c.empire});
    ImGui::Text("Founded %s", formatDate(c.foundedDay).c_str());
  } else {
    ImGui::TextDisabled("Not yet spacefaring");
  }
}

void Viewer::drawSpeciesDetails(const Species& sp) {
  ImGui::SeparatorText(sp.name.c_str());
  ImGui::Text("Homeworld");
  ImGui::SameLine();
  entityLink({EntityKind::Body, sp.homeworld});
  ImGui::Text("Prefers %.0f K (+/- %.0f), %.2f g", sp.prefTempK, sp.tempTolK, sp.prefGravity);
  ImGui::Text("Growth %.1f%%/yr, tech aptitude %.2f", sp.growthRate * 100.0, sp.techAptitude);
  if (sp.spacefaring) {
    ImGui::Text("Spacefaring as");
    ImGui::SameLine();
    entityLink({EntityKind::Empire, sp.empire});
  } else {
    ImGui::Text("Progress toward spaceflight");
    ImGui::ProgressBar(static_cast<float>(std::min(sp.techProgress, 1.0)), ImVec2(-1, 0));
  }
}

void Viewer::drawEmpireDetails(const Empire& e) {
  ImGui::PushStyleColor(ImGuiCol_Text, empireColor(e.id));
  ImGui::SeparatorText(e.name.c_str());
  ImGui::PopStyleColor();
  ImGui::Text("Founded %s by the", formatDate(e.foundedDay).c_str());
  ImGui::SameLine();
  entityLink({EntityKind::Species, e.species});
  ImGui::Text("Capital");
  ImGui::SameLine();
  entityLink({EntityKind::Body, at(w_.colonies, e.capital).body});
  ImGui::SameLine();
  if (ImGui::SmallButton("Center on map")) focusOn({EntityKind::Empire, e.id});

  const auto ships = std::count_if(w_.ships.begin(), w_.ships.end(), [&](const ColonyShip& s) { return s.empire == e.id; });
  ImGui::Text("Population %.2f billion on %zu worlds", empirePopulation(e) / 1000.0, e.colonies.size());
  ImGui::Text("Mineral stockpile %.0f, colony ships in flight %lld", e.minerals, static_cast<long long>(ships));
  ImGui::Text("Expansion drive %.0f%%", e.expansionDrive * 100.0);

  const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
  if (ImGui::BeginTable("colonies", 3, flags)) {
    ImGui::TableSetupColumn("World");
    ImGui::TableSetupColumn("Pop (B)");
    ImGui::TableSetupColumn("Founded");
    ImGui::TableHeadersRow();
    for (Id cid : e.colonies) {
      const Colony& c = at(w_.colonies, cid);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      entityLink({EntityKind::Body, c.body});
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", c.population / 1000.0);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(formatDate(c.foundedDay).c_str());
    }
    ImGui::EndTable();
  }
}

void Viewer::drawHistory() {
  const auto& events = w_.log.events();
  std::vector<std::size_t> hits;
  for (std::size_t i = events.size(); i-- > 0 && hits.size() < 200;) {
    if (eventMentionsSelection(events[i])) hits.push_back(i);
  }
  ImGui::SeparatorText("History");
  if (hits.empty()) {
    ImGui::TextDisabled("Nothing recorded yet.");
    return;
  }
  ImGui::BeginChild("history", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
  for (std::size_t i : hits) eventRow(events[i], i + 1000000000u);  // distinct ids from the feed
  ImGui::EndChild();
}

// ---- Helpers ---------------------------------------------------------------

bool Viewer::eventMentionsSelection(const Event& e) const {
  for (const EntityRef& r : e.refs) {
    switch (selection_.kind) {
      case EntityKind::System:
        if (sameRef(r, EntityKind::System, selection_.id)) return true;
        if (r.kind == EntityKind::Body && at(w_.bodies, r.id).system == selection_.id) return true;
        break;
      case EntityKind::Species:
        if (sameRef(r, EntityKind::Species, selection_.id)) return true;
        if (r.kind == EntityKind::Empire && at(w_.empires, r.id).species == selection_.id) return true;
        break;
      default:
        if (sameRef(r, selection_.kind, selection_.id)) return true;
        break;
    }
  }
  return false;
}

Id Viewer::systemOf(EntityRef ref) const {
  switch (ref.kind) {
    case EntityKind::System: return ref.id;
    case EntityKind::Body: return at(w_.bodies, ref.id).system;
    case EntityKind::Colony: return at(w_.bodies, at(w_.colonies, ref.id).body).system;
    case EntityKind::Species: return at(w_.bodies, at(w_.species, ref.id).homeworld).system;
    case EntityKind::Empire: return at(w_.bodies, at(w_.colonies, at(w_.empires, ref.id).capital).body).system;
  }
  return kNone;
}

void Viewer::select(EntityRef ref) {
  if (ref.kind == EntityKind::Colony) ref = {EntityKind::Body, at(w_.colonies, ref.id).body};
  selection_ = {ref.kind, ref.id};
}

void Viewer::focusOn(EntityRef ref) {
  const Id sys = systemOf(ref);
  if (sys == kNone) return;
  camX_ = at(w_.systems, sys).x;
  camY_ = at(w_.systems, sys).y;
  scale_ = std::max(scale_, 8.0);
}

void Viewer::entityLink(EntityRef ref) {
  ImGui::PushID(static_cast<int>(ref.kind));
  ImGui::PushID(ref.id);
  const std::string name = w_.nameOf(ref);
  if (ImGui::TextLink(name.c_str())) select(ref);
  ImGui::PopID();
  ImGui::PopID();
}
