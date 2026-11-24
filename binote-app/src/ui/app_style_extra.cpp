// Binote - www.rebisway.com - contact@rebisway.com
// Copyright (C) 2026 Thomas Gourgnier
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// Binote - style analysis: source against destination, and the evolution of the indication through the saved versions

#include "ui/app.h"
#include "common/i18n.h"
#include <algorithm>

// Extra pages of the style window: source / destination comparison and the evolution of the index over the history
void App::draw_style_extra()
{
	const std::string language = _has_project ? _project.language : "fr";
	if (ImGui::CollapsingHeader(TR("STYLE_COMPARE")))
	{
		if (ImGui::Button(TR("STYLE_COMPARE_RUN")) || !_style_cmp_done)
		{
			_style_cmp_src = style::analyze(editor_lines(_src.editor), _s.markdown, language);
			_style_cmp_dst = style::analyze(editor_lines(_dst.editor), _s.markdown, language);
			_style_cmp_done = true;
		}
		const style::Metrics& a = _style_cmp_src.metrics;
		const style::Metrics& b = _style_cmp_dst.metrics;
		if (ImGui::BeginTable("##style_compare", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
		{
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn(TR("PANE_SOURCE_SHORT"), ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableSetupColumn(TR("PANE_DESTINATION_SHORT"), ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableSetupColumn(TR("STATS_DIFFERENCE"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableHeadersRow();
			auto row = [&](const char* label, double x, double y, const char* format)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::TextUnformatted(label);
				char buf[48];
				ImGui::TableNextColumn(); snprintf(buf, sizeof(buf), format, x); ImGui::TextUnformatted(buf);
				ImGui::TableNextColumn(); snprintf(buf, sizeof(buf), format, y); ImGui::TextUnformatted(buf);
				ImGui::TableNextColumn(); snprintf(buf, sizeof(buf), (std::string("%+") + (format + 1)).c_str(), y - x); ImGui::TextUnformatted(buf);
			};
			row(TR("STYLE_COMPARE_SCORE"), _style_cmp_src.score.value, _style_cmp_dst.score.value, "%.0f");
			row(TR("STATS_WORDS"), a.words, b.words, "%.0f");
			row(TR("STYLE_MEAN_SENTENCE"), a.mean_sentence, b.mean_sentence, "%.1f");
			row(TR("STYLE_SENTENCE_REGULARITY"), a.sentence_variation, b.sentence_variation, "%.2f");
			row(TR("STYLE_LEXICAL_DIVERSITY"), a.lexical_diversity * 100.0, b.lexical_diversity * 100.0, "%.0f");
			row(TR("STYLE_CONNECTORS"), a.connectors_per_1000, b.connectors_per_1000, "%.1f");
			row(TR("STYLE_STOCK_PHRASES"), a.stock_phrases_per_1000, b.stock_phrases_per_1000, "%.1f");
			row(TR("STYLE_LONG_SENTENCES"), a.long_sentences, b.long_sentences, "%.0f");
			row(TR("STYLE_PREDICTABLE"), a.predictable_share * 100.0, b.predictable_share * 100.0, "%.0f");
			ImGui::EndTable();
		}
	}
	if (ImGui::CollapsingHeader(TR("STYLE_TIMELINE")))
	{
		if (ImGui::Button(TR("STYLE_TIMELINE_RUN")))
		{
			_style_timeline.clear();
			_style_timeline_labels.clear();
			if (const Doc* d = current_doc())
			{
				std::vector<History::Snapshot> snaps = History::list(d->dst_path);
				if (snaps.size() > 60)
					snaps.resize(60);
				std::reverse(snaps.begin(), snaps.end());			// oldest first
				for (const History::Snapshot& s : snaps)
				{
					const std::vector<std::string> lines = util::split_lines(History::read(s));
					_style_timeline.push_back((float)style::analyze(lines, _s.markdown, language).score.value);
					_style_timeline_labels.push_back(s.label);
				}
				_style_timeline.push_back((float)style::analyze(editor_lines(_dst.editor), _s.markdown, language).score.value);
				_style_timeline_labels.push_back(TR("STYLE_TIMELINE_NOW"));
			}
		}
		if (_style_timeline.size() > 1)
		{
			ImGui::PlotLines("##timeline", _style_timeline.data(), (int)_style_timeline.size(), 0, nullptr, 0.0f, 100.0f, ImVec2(-1.0f, 90.0f));
			if (ImGui::IsItemHovered())
			{
				const float t = (ImGui::GetIO().MousePos.x - ImGui::GetItemRectMin().x) / std::max(1.0f, ImGui::GetItemRectSize().x);
				const int i = std::max(0, std::min((int)_style_timeline.size() - 1, (int)(t * (_style_timeline.size() - 1) + 0.5f)));
				ImGui::SetTooltip("%s : %.0f / 100", _style_timeline_labels[(size_t)i].c_str(), _style_timeline[(size_t)i]);
			}
		}
		else if (!_style_timeline.empty() || true)
			ImGui::TextDisabled("%s", TR("STYLE_TIMELINE_HELP"));
	}
}
