// Binote - version control window (Git / Subversion)
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "app.h"
#include "i18n.h"
#include "../common/logger.h"
#include <filesystem>

namespace fs = std::filesystem;

void App::vcs_refresh()
{
	if (!_has_project)
		return;
	_vcs = vcs::detect(_project.source_root);
	_vcs_changes.clear();
	if (_vcs.kind != vcs::None)
	{
		std::string out;
		_vcs_changes = vcs::status(_vcs, _project.source_root, out);
		if (_vcs_changes.empty() && !out.empty())
			_vcs_output = out;
	}
}

void App::reload_current()
{
	if (_has_project)
		open_doc(_cur, 0);
}

// File of the current document, relative to the folder of the project (the destination, which is the one which changes)
static std::string current_relative(const Doc* d, const std::string& root)
{
	if (!d)
		return "";
	std::error_code ec;
	const fs::path rel = fs::relative(fs::u8path(d->dst_path), fs::u8path(root), ec);
	return ec ? d->rel : rel.generic_u8string();
}

void App::draw_vcs()
{
	if (!_has_project)
	{
		_show_vcs = false;
		return;
	}
	ImGui::SetNextWindowSize(ImVec2(640.0f, 520.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->WorkPos.x + 260.0f, ImGui::GetMainViewport()->WorkPos.y + 80.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(TR("VCS_TITLE"), &_show_vcs))
	{
		ImGui::End();
		return;
	}
	if (ImGui::IsWindowAppearing())
		vcs_refresh();
	const char* kind = _vcs.kind == vcs::Git ? "Git" : (_vcs.kind == vcs::Svn ? "Subversion" : "");
	if (_vcs.kind == vcs::None)
	{
		ImGui::TextWrapped("%s", TR("VCS_NONE"));
		if (ImGui::Button(TR("VCS_INIT_GIT")))
		{
			std::string out;
			vcs::init(_project.source_root, out);
			_vcs_output = out;
			vcs_refresh();
		}
		ImGui::SameLine();
		if (ImGui::Button(TR("VCS_REFRESH")))
			vcs_refresh();
		if (!_vcs_output.empty())
			ImGui::TextWrapped("%s", _vcs_output.c_str());
		ImGui::End();
		return;
	}
	ImGui::Text("%s", i18n::fmt(TR("VCS_REPOSITORY"), kind, _vcs.branch.empty() ? "-" : _vcs.branch.c_str(), _vcs.root.c_str()).c_str());
	if (!_vcs.program_found)
		ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", TR("VCS_PROGRAM_MISSING"));

	const std::string file = current_relative(current_doc(), _project.source_root);
	std::string out;
	auto done = [&](bool ok, const char* what)
	{
		_vcs_output = out.empty() ? std::string(ok ? "OK" : "Failed") : out;
		Logger::log(std::string("VCS ") + what + ": " + (ok ? "ok" : "failed"));
		vcs_refresh();
		if (!ok && _vcs_output.empty())
			_vcs_output = "Failed";
	};
	if (ImGui::Button(TR("VCS_REFRESH")))
		vcs_refresh();
	ImGui::SameLine();
	if (ImGui::Button(TR("VCS_UPDATE")))
	{
		if (dirty())
			save();
		const bool ok = vcs::update(_vcs, _project.source_root, out);
		done(ok, "update");
		reload_current();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(_vcs.kind != vcs::Git);
	if (ImGui::Button(TR("VCS_PUSH")))
	{
		const bool ok = vcs::push(_vcs, _project.source_root, out);
		done(ok, "push");
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button(TR("VCS_LOG_FILE")))
	{
		const bool ok = vcs::log(_vcs, _project.source_root, file, out);
		_vcs_output = out;
		(void)ok;
	}
	ImGui::SameLine();
	if (ImGui::Button(TR("VCS_DIFF_FILE")))
	{
		vcs::diff(_vcs, _project.source_root, file, out);
		_vcs_output = out.empty() ? std::string(TR("VCS_NO_DIFF")) : out;
	}

	ImGui::Separator();
	ImGui::Text("%s", i18n::fmt(TR("VCS_CHANGES"), (int)_vcs_changes.size()).c_str());
	ImGui::BeginChild("##vcs_changes", ImVec2(0, 140.0f), true);
	for (const vcs::Change& c : _vcs_changes)
	{
		const ImVec4 col = c.status.find('M') != std::string::npos ? ImVec4(0.95f, 0.75f, 0.3f, 1) : (c.status.find('D') != std::string::npos ? ImVec4(0.95f, 0.4f, 0.4f, 1) : ImVec4(0.45f, 0.85f, 0.5f, 1));
		ImGui::TextColored(col, "%-2s", c.status.c_str());
		ImGui::SameLine();
		if (ImGui::Selectable((c.path + "##change" + c.path).c_str()))
		{
			vcs::diff(_vcs, _project.source_root, c.path, out);
			_vcs_output = out.empty() ? std::string(TR("VCS_NO_DIFF")) : out;
		}
	}
	ImGui::EndChild();

	ImGui::SetNextItemWidth(-130.0f);
	ImGui::InputTextWithHint("##vcs_message", TR("VCS_MESSAGE"), _vcs_message, sizeof(_vcs_message));
	ImGui::SameLine();
	ImGui::BeginDisabled(_vcs_message[0] == 0);
	if (ImGui::Button(TR("VCS_COMMIT"), ImVec2(120.0f, 0.0f)))
	{
		if (dirty())
			save();
		const bool ok = vcs::commit(_vcs, _project.source_root, _vcs_message, out);
		if (ok)
			_vcs_message[0] = 0;
		done(ok, "commit");
	}
	ImGui::EndDisabled();
	ImGui::Separator();
	ImGui::BeginChild("##vcs_output", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
	ImGui::PushFont(_fonts.mono ? _fonts.mono : ImGui::GetFont());
	ImGui::TextUnformatted(_vcs_output.c_str());
	ImGui::PopFont();
	ImGui::EndChild();
	ImGui::End();
}
