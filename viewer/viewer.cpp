#include "viewer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>

#include "gl_texture.h"
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

ImVec4 empireRGB(Id id) {
  const float hue = std::fmod(static_cast<float>(id) * 0.618034f + 0.08f, 1.0f);
  ImVec4 c(0, 0, 0, 1);
  ImGui::ColorConvertHSVtoRGB(hue, 0.65f, 0.95f, c.x, c.y, c.z);
  return c;
}

ImU32 empireColor(Id id, float alpha = 1.0f) {
  ImVec4 c = empireRGB(id);
  c.w = alpha;
  return ImGui::ColorConvertFloat4ToU32(c);
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

const char* memoryPhrase(MemoryKind k) {
  switch (k) {
    case MemoryKind::PassedOver: return "Passed over in favour of";
    case MemoryKind::Favoured: return "Favoured by";
    case MemoryKind::Quarrel: return "Quarrelled with";
    case MemoryKind::Friendship: return "Grew close to";
    case MemoryKind::KinKilled: return "Kin killed by";
    case MemoryKind::Overthrown: return "House overthrown by";
    case MemoryKind::PlotAgainstMe: return "Plotted against by";
    case MemoryKind::Spared: return "Spared by";
  }
  return "?";
}

const char* opinionWord(double v) {
  if (v <= -40) return "Rival";
  if (v <= -15) return "Dislikes";
  if (v >= 40) return "Friend";
  if (v >= 15) return "Likes";
  return "Neutral";
}

}  // namespace

Viewer::Viewer(World& world, Sim& sim, std::string title)
    : w_(world), sim_(sim), title_(std::move(title)), resolve_([this](EntityRef r) { return w_.nameOf(r); }) {}

Viewer::~Viewer() {
  if (territoryTexture_ != 0) gltex::destroy(territoryTexture_);
}

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
  updateTerritory();

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

void Viewer::updateTerritory() {
  const std::uint64_t signature = territorySignature(w_);
  if (territoryValid_ && signature == territorySignature_) return;
  territorySignature_ = signature;
  territoryValid_ = true;
  territory_ = computeTerritory(w_);

  // Fill: a soft tint per empire, uploaded as one texture.
  const int width = territory_.width, height = territory_.height;
  std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const Id owner = territory_.at(x, y);
      if (owner == kNone) continue;
      const ImVec4 c = empireRGB(owner);
      pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
          IM_COL32(static_cast<int>(c.x * 255), static_cast<int>(c.y * 255), static_cast<int>(c.z * 255), 48);
    }
  }
  if (territoryTexture_ == 0) territoryTexture_ = gltex::create();
  gltex::upload(territoryTexture_, width, height, pixels.data());

  // Outlines: marching squares over cell centres, per empire, so borders are
  // drawn as lines that stay crisp at any zoom.
  // Corner bits: 1 top-left, 2 top-right, 4 bottom-right, 8 bottom-left.
  // Edges: 0 top, 1 right, 2 bottom, 3 left. Each case lists edge pairs to join.
  static const int kCases[16][4] = {
      {-1, -1, -1, -1}, {3, 0, -1, -1}, {0, 1, -1, -1}, {3, 1, -1, -1}, {1, 2, -1, -1}, {3, 0, 1, 2},
      {0, 2, -1, -1},   {3, 2, -1, -1}, {2, 3, -1, -1}, {0, 2, -1, -1}, {0, 1, 2, 3},  {1, 2, -1, -1},
      {3, 1, -1, -1},   {0, 1, -1, -1}, {3, 0, -1, -1}, {-1, -1, -1, -1}};
  const double cs = territory_.cellSize, ox = territory_.originX, oy = territory_.originY;
  auto edgePoint = [&](int x, int y, int edge, double& px, double& py) {
    // Cell centres sit at (i + 0.5); edge midpoints lie between two of them.
    double u = 0, v = 0;
    switch (edge) {
      case 0: u = x + 1.0; v = y + 0.5; break;
      case 1: u = x + 1.5; v = y + 1.0; break;
      case 2: u = x + 1.0; v = y + 1.5; break;
      default: u = x + 0.5; v = y + 1.0; break;
    }
    px = ox + u * cs;
    py = oy + v * cs;
  };

  borders_.clear();
  for (int y = -1; y < height; ++y) {
    for (int x = -1; x < width; ++x) {
      const Id corners[4] = {territory_.at(x, y), territory_.at(x + 1, y), territory_.at(x + 1, y + 1),
                             territory_.at(x, y + 1)};
      if (corners[0] == corners[1] && corners[1] == corners[2] && corners[2] == corners[3]) continue;
      for (int k = 0; k < 4; ++k) {
        const Id e = corners[k];
        if (e == kNone) continue;
        bool seen = false;  // trace each empire once per square
        for (int j = 0; j < k; ++j) seen = seen || corners[j] == e;
        if (seen) continue;
        int mask = 0;
        for (int j = 0; j < 4; ++j) {
          if (corners[j] == e) mask |= 1 << j;
        }
        const int* pairs = kCases[mask];
        for (int p = 0; p < 4 && pairs[p] >= 0; p += 2) {
          BorderSegment s;
          s.empire = e;
          edgePoint(x, y, pairs[p], s.x0, s.y0);
          edgePoint(x, y, pairs[p + 1], s.x1, s.y1);
          borders_.push_back(s);
        }
      }
    }
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
    if (hovered == kNone && showBorders_ && territoryValid_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      const double wx = camX_ + (mouse.x - center.x) / scale_;
      const double wy = camY_ + (mouse.y - center.y) / scale_;
      const Id claimant = territory_.at(static_cast<int>((wx - territory_.originX) / territory_.cellSize),
                                        static_cast<int>((wy - territory_.originY) / territory_.cellSize));
      if (claimant != kNone) {
        ImGui::BeginTooltip();
        ImGui::TextColored(empireRGB(claimant), "%s space", at(w_.empires, claimant).name.c_str());
        ImGui::EndTooltip();
      }
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

  if (showBorders_ && territoryTexture_ != 0) {
    const ImVec2 p0 = toScreen(territory_.originX, territory_.originY);
    const ImVec2 p1 = toScreen(territory_.originX + territory_.width * territory_.cellSize,
                               territory_.originY + territory_.height * territory_.cellSize);
    dl->AddImage(static_cast<ImTextureID>(static_cast<std::intptr_t>(territoryTexture_)), p0, p1);
    for (const BorderSegment& s : borders_) {
      const ImVec2 a = toScreen(s.x0, s.y0);
      if (!onScreen(a)) continue;
      dl->AddLine(a, toScreen(s.x1, s.y1), empireColor(s.empire, 0.9f), 1.5f);
    }
  }

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
  ImGui::Checkbox("Borders", &showBorders_);
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
  if (ImGui::BeginTable("empires", 5, flags)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Empire", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("Worlds", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Pop (B)", ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn("Minerals", ImGuiTableColumnFlags_WidthStretch, 1.3f);
    ImGui::TableSetupColumn("Ruler", ImGuiTableColumnFlags_WidthStretch, 2.5f);
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
      ImGui::TableNextColumn();
      if (e->ruler != kNone) entityLink({EntityKind::Figure, e->ruler});
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
    case EntityKind::Figure: drawFigureDetails(at(w_.figures, selection_.id)); break;
    case EntityKind::Organization: drawHouseDetails(at(w_.organizations, selection_.id)); break;
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
    ImGui::Text("Governor");
    ImGui::SameLine();
    if (c.governor != kNone) entityLink({EntityKind::Figure, c.governor});
    else ImGui::TextDisabled("vacant");
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
  ImGui::Text("Expansion drive %.0f%% (culture), shaped by the ruler's ambition", e.expansionDrive * 100.0);

  ImGui::SeparatorText("Court");
  ImGui::Text("%s, %s succession", e.government.c_str(), e.hereditary ? "hereditary" : "elective");
  ImGui::Text("Ruler");
  ImGui::SameLine();
  if (e.ruler != kNone) entityLink({EntityKind::Figure, e.ruler});
  else ImGui::TextDisabled("vacant");
  if (e.ruler != kNone) {
    ImGui::SameLine();
    ImGui::TextDisabled("since %s", formatDate(e.rulerSinceDay).c_str());
  }
  bar("Legitimacy", static_cast<float>(e.legitimacy), 100.0f);
  ImGui::Text("High Admiral");
  ImGui::SameLine();
  if (e.admiral != kNone) entityLink({EntityKind::Figure, e.admiral});
  else ImGui::TextDisabled("vacant");
  ImGui::TextDisabled("Houses:");
  for (Id h : e.houses) {
    if (at(w_.organizations, h).endedDay >= 0) continue;
    ImGui::SameLine();
    entityLink({EntityKind::Organization, h});
  }

  const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
  if (ImGui::BeginTable("colonies", 4, flags)) {
    ImGui::TableSetupColumn("World");
    ImGui::TableSetupColumn("Pop (B)");
    ImGui::TableSetupColumn("Founded");
    ImGui::TableSetupColumn("Governor");
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
      ImGui::TableNextColumn();
      if (c.governor != kNone) entityLink({EntityKind::Figure, c.governor});
    }
    ImGui::EndTable();
  }
}

void Viewer::bar(const char* label, float value, float max) {
  ImGui::TextUnformatted(label);
  ImGui::SameLine(110);
  char overlay[16];
  std::snprintf(overlay, sizeof overlay, "%.0f", value);
  ImGui::ProgressBar(value / max, ImVec2(-1, 0), overlay);
}

void Viewer::drawFigureDetails(const Figure& f) {
  ImGui::SeparatorText(f.name.c_str());
  const Empire& e = at(w_.empires, f.empire);
  switch (f.role) {
    case Role::Ruler: ImGui::Text("Ruler of the"); break;
    case Role::Admiral: ImGui::Text("High Admiral of the"); break;
    case Role::Governor:
      ImGui::Text("Governor of");
      ImGui::SameLine();
      entityLink({EntityKind::Body, at(w_.colonies, f.seat).body});
      ImGui::SameLine();
      ImGui::Text("in the");
      break;
    case Role::None: ImGui::Text("%s of the", f.alive() ? "Subject" : "Late subject"); break;
  }
  ImGui::SameLine();
  entityLink({EntityKind::Empire, e.id});
  if (f.role != Role::None || f.house != kNone) {
    ImGui::SameLine();
    if (ImGui::SmallButton("Center on map")) focusOn({EntityKind::Figure, f.id});
  }

  if (f.house != kNone) {
    ImGui::Text("Of");
    ImGui::SameLine();
    entityLink({EntityKind::Organization, f.house});
  } else {
    ImGui::TextDisabled("A commoner, of no great house");
  }
  ImGui::SameLine();
  ImGui::TextDisabled("(");
  ImGui::SameLine(0, 0);
  entityLink({EntityKind::Species, f.species});
  ImGui::SameLine(0, 0);
  ImGui::TextDisabled(")");

  if (f.alive()) {
    ImGui::Text("Born %s, age %d", formatDate(f.bornDay).c_str(), ageInYears(f, w_.day));
  } else {
    ImGui::Text("Born %s, died %s aged %d", formatDate(f.bornDay).c_str(), formatDate(f.diedDay).c_str(),
                ageInYears(f, w_.day));
  }
  if (f.parent != kNone) {
    ImGui::Text("Parent");
    ImGui::SameLine();
    entityLink({EntityKind::Figure, f.parent});
  }
  if (!f.children.empty()) {
    ImGui::TextDisabled("Children:");
    for (Id c : f.children) {
      ImGui::SameLine();
      entityLink({EntityKind::Figure, c});
      if (!at(w_.figures, c).alive()) {
        ImGui::SameLine(0, 2);
        ImGui::TextDisabled("(d.)");
      }
    }
  }

  ImGui::SeparatorText("Character");
  bar("Administration", static_cast<float>(f.administration), 20.0f);
  bar("Military", static_cast<float>(f.military), 20.0f);
  bar("Intrigue", static_cast<float>(f.intrigue), 20.0f);
  bar("Ambition", static_cast<float>(f.ambition * 100.0), 100.0f);
  bar("Loyalty", static_cast<float>(f.loyalty * 100.0), 100.0f);
  bar("Ruthlessness", static_cast<float>(f.ruthlessness * 100.0), 100.0f);

  // Strongest feelings first.
  std::vector<Opinion> feelings = f.opinions;
  std::sort(feelings.begin(), feelings.end(),
            [](const Opinion& a, const Opinion& b) { return std::abs(a.value) > std::abs(b.value); });
  if (feelings.size() > 10) feelings.resize(10);
  feelings.erase(std::remove_if(feelings.begin(), feelings.end(), [](const Opinion& o) { return std::abs(o.value) < 5; }),
                 feelings.end());
  ImGui::SeparatorText("Relationships");
  if (feelings.empty()) ImGui::TextDisabled("No strong feelings about anyone.");
  const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
  if (!feelings.empty() && ImGui::BeginTable("opinions", 3, flags)) {
    for (const Opinion& o : feelings) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      entityLink({EntityKind::Figure, o.of});
      ImGui::TableNextColumn();
      const ImVec4 colour = o.value < 0 ? ImVec4(0.95f, 0.45f, 0.4f, 1) : ImVec4(0.5f, 0.85f, 0.5f, 1);
      ImGui::TextColored(colour, "%+.0f", o.value);
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", opinionWord(o.value));
    }
    ImGui::EndTable();
  }

  ImGui::SeparatorText("Memories");
  if (f.memories.empty()) ImGui::TextDisabled("Nothing of note.");
  for (std::size_t i = f.memories.size(); i-- > 0;) {
    const Memory& m = f.memories[i];
    ImGui::PushID(static_cast<int>(i));
    ImGui::TextDisabled("%s", formatDate(m.day).c_str());
    ImGui::SameLine();
    ImGui::TextUnformatted(memoryPhrase(m.kind));
    ImGui::SameLine();
    entityLink({EntityKind::Figure, m.about});
    ImGui::PopID();
  }
}

void Viewer::drawHouseDetails(const Organization& o) {
  ImGui::SeparatorText(o.name.c_str());
  ImGui::Text("A noble house of the");
  ImGui::SameLine();
  entityLink({EntityKind::Empire, o.empire});
  if (o.founder != kNone) {
    ImGui::Text("Founded by");
    ImGui::SameLine();
    entityLink({EntityKind::Figure, o.founder});
  }
  if (o.endedDay >= 0) {
    ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.4f, 1), "Died out %s", formatDate(o.endedDay).c_str());
  } else {
    bar("Prestige", static_cast<float>(o.prestige), 100.0f);
  }

  std::vector<Id> living;
  int dead = 0;
  for (Id m : o.members) {
    if (at(w_.figures, m).alive()) living.push_back(m);
    else ++dead;
  }
  std::sort(living.begin(), living.end(),
            [this](Id a, Id b) { return at(w_.figures, a).bornDay < at(w_.figures, b).bornDay; });

  ImGui::SeparatorText("Members");
  ImGui::TextDisabled("%zu living, %d dead", living.size(), dead);
  const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
  if (!living.empty() && ImGui::BeginTable("members", 3, flags)) {
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Age");
    ImGui::TableSetupColumn("Office");
    ImGui::TableHeadersRow();
    for (Id m : living) {
      const Figure& f = at(w_.figures, m);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      entityLink({EntityKind::Figure, m});
      ImGui::TableNextColumn();
      ImGui::Text("%d", ageInYears(f, w_.day));
      ImGui::TableNextColumn();
      if (f.role != Role::None) ImGui::TextUnformatted(roleName(f.role));
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
      case EntityKind::Organization:
        if (sameRef(r, EntityKind::Organization, selection_.id)) return true;
        if (r.kind == EntityKind::Figure && at(w_.figures, r.id).house == selection_.id) return true;
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
    case EntityKind::Figure: {
      const Figure& f = at(w_.figures, ref.id);
      if (f.role == Role::Governor) return systemOf({EntityKind::Colony, f.seat});
      return systemOf({EntityKind::Empire, f.empire});
    }
    case EntityKind::Organization: return systemOf({EntityKind::Empire, at(w_.organizations, ref.id).empire});
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
