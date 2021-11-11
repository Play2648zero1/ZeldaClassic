#ifndef ZC_DIALOG_OPTIONS_H
#define ZC_DIALOG_OPTIONS_H

#include <gui/dialog.h>
#include <gui/text_field.h>
#include <functional>
#include <string_view>

void call_options_dlg();

class OptionsDialog: public GUI::Dialog<OptionsDialog>
{
public:
	enum class message { OK, CANCEL };

	OptionsDialog();

	std::shared_ptr<GUI::Widget> view() override;
	bool handleMessage(const GUI::DialogMessage<message>& msg);

private:
	enum optionVal
	{
		OPT_MOUSESCROLL, OPT_SAVEPATHS, OPT_PALCYCLE, OPT_VSYNC, OPT_FPS, OPT_COMB_BRUSH,
		OPT_FLOAT_BRUSH, OPT_RELOAD_QUEST, OPT_MISALIGNS, OPT_ANIM_COMBOS, OPT_OW_PROT,
		OPT_TILE_PROT, OPT_STATIC_INVAL, OPT_SMALLMODE, OPT_RULESET, OPT_TOOLTIPS,
		OPT_PATTERNSEARCH, OPT_NEXTPREVIEW, OPT_INITSCR_WARN,
		//
		OPT_ABRETENTION, OPT_ASINTERVAL, OPT_ASRETENTION, OPT_UNCOMP_AUTOSAVE, OPT_GRIDCOL,
		OPT_SNAPFORMAT, OPT_KBREPDEL, OPT_KBREPRATE,
		//
		OPT_CURS_LARGE, OPT_CURS_SMALL,
		//
		OPT_MAX
	};
	int32_t opts[OPT_MAX];
	void loadOptions();
	void saveOptions();
};

#endif