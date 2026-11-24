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

// Binote - export of the project (dialog box)

#include "ui/app.h"
#include "export/exporter.h"
#include "common/i18n.h"
#include "common/dialog.h"
#include "common/logger.h"
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

// The text of each file of the project: the destination (the editor for the current one), else the source
static std::string export_title_of(const std::string& text, const std::string& fallback)
{
	size_t pos = 0;
	while (pos < text.size())
	{
		size_t eol = text.find('\n', pos);
		if (eol == std::string::npos)
			eol = text.size();
		std::string line = text.substr(pos, eol - pos);
		pos = eol + 1;
		size_t a = line.find_first_not_of(" \t#");
		if (!line.empty() && line.find('#') != std::string::npos && line.find_first_not_of(" \t") == line.find('#') && a != std::string::npos)
		{
			size_t b = line.find_last_not_of(" \t\r#");
			return line.substr(a, b - a + 1);
		}
	}
	return fallback;
}

// Export window: format (PDF, Word, EPUB, HTML, one Markdown), title page, page breaks, source or destination
void App::draw_export()
{
	if (_open_export)
	{
		_open_export = false;
		ImGui::OpenPopup("###export");
		if (_export_title[0] == 0)
			snprintf(_export_title, sizeof(_export_title), "%s", _project.name.c_str());
		_export_message.clear();
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Appearing);
	const std::string title = std::string(TR("EXPORT_TITLE")) + "###export";
	if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings))
		return;
	static const char* const formats[] = { "EXPORT_PDF", "EXPORT_DOCX", "EXPORT_EPUB", "EXPORT_HTML", "EXPORT_MARKDOWN" };
	static const exporter::Format values[] = { exporter::Pdf, exporter::Docx, exporter::Epub, exporter::Html, exporter::Markdown };
	ImGui::TextUnformatted(TR("EXPORT_FORMAT"));
	ImGui::SetNextItemWidth(-1);
	if (ImGui::BeginCombo("##format", TR(formats[_s.export_format])))
	{
		for (int i = 0; i < 5; i++)
			if (ImGui::Selectable(TR(formats[i]), i == _s.export_format))
			{
				_s.export_format = i;
				_export_path.clear();
				_s.save();
			}
		ImGui::EndCombo();
	}
	ImGui::TextUnformatted(TR("EXPORT_BOOK_TITLE"));
	ImGui::SetNextItemWidth(-1);
	ImGui::InputText("##book_title", _export_title, sizeof(_export_title));
	ImGui::TextUnformatted(TR("EXPORT_AUTHOR"));
	ImGui::SetNextItemWidth(-1);
	char author[128];
	snprintf(author, sizeof(author), "%s", _s.export_author.c_str());
	if (ImGui::InputText("##author", author, sizeof(author)))
		_s.export_author = author;
	ImGui::Checkbox(TR("EXPORT_TITLE_PAGE"), &_export_title_page);
	ImGui::Checkbox(TR("EXPORT_PAGE_BREAK"), &_export_page_break);
	ImGui::Checkbox(TR("EXPORT_SOURCE"), &_export_source);
	ImGui::Separator();
	const exporter::Format format = values[_s.export_format];
	if (_export_path.empty())
	{
		const std::string base = _project.is_folder ? fs::u8path(_project.source_root).filename().u8string() : fs::u8path(_project.docs.empty() ? "export" : _project.docs[0].rel).stem().u8string();
		_export_path = util::combine(_project.is_folder ? _project.dest_root : util::parent_path(_project.source_root + "/x"), (base.empty() ? "export" : base) + exporter::extension(format));
	}
	ImGui::TextUnformatted(TR("EXPORT_FILE"));
	char path[512];
	snprintf(path, sizeof(path), "%s", _export_path.c_str());
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 40.0f);
	if (ImGui::InputText("##export_path", path, sizeof(path)))
		_export_path = path;
	ImGui::SameLine();
	if (ImGui::Button("...##export_browse"))
	{
		std::string chosen = _export_path;
		const std::string ext = exporter::extension(format);
		if (dialog::save_file_dialog(_export_path, chosen, "*" + ext, "*" + ext))
		{
			if (fs::u8path(chosen).extension().empty())
				chosen += ext;
			_export_path = chosen;
		}
	}
	if (!_export_message.empty())
		ImGui::TextWrapped("%s", _export_message.c_str());
	ImGui::Spacing();
	if (ImGui::Button(TR("EXPORT_DO"), ImVec2(120.0f, 0.0f)))
	{
		std::vector<exporter::Chapter> chapters;
		for (int i = 0; i < (int)_project.docs.size(); i++)
		{
			const Doc& d = _project.docs[i];
			std::string text;
			if (!_export_source && i == _cur)
				text = editor_text(_dst.editor);
			else if (_export_source && i == _cur)
				text = editor_text(_src.editor);
			else if (_export_source || !(util::exists(d.dst_path) && util::read_text(d.dst_path, text)))
				util::read_text(d.src_path, text);
			exporter::Chapter c;
			c.text = text;
			c.title = export_title_of(text, fs::u8path(d.rel).stem().u8string());
			chapters.push_back(c);
		}
		exporter::Options o;
		o.format = format;
		o.title = _export_title;
		o.author = _s.export_author;
		o.language = _project.language;
		o.markdown = _s.markdown;
		o.page_break = _export_page_break;
		o.title_page = _export_title_page;
		std::string error;
		if (exporter::write(_export_path, chapters, o, error))
		{
			Logger::log("Export: " + _export_path);
			status(i18n::fmt(TR("MSG_EXPORTED"), _export_path.c_str()));
			_s.save();
			ImGui::CloseCurrentPopup();
		}
		else
			_export_message = error;
	}
	ImGui::SameLine();
	if (ImGui::Button(TR("BTN_CANCEL"), ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
		ImGui::CloseCurrentPopup();
	ImGui::SameLine();
	if (ImGui::Button(TR("EXPORT_OPEN_FOLDER")) && !_export_path.empty())
		dialog::open_folder(util::parent_path(_export_path));
	ImGui::EndPopup();
}
