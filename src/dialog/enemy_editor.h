#ifndef ZC_DIALOG_ENEMYEDITOR_H
#define ZC_DIALOG_ENEMYEDITOR_H

#include <gui/dialog.h>
#include <gui/checkbox.h>
#include <gui/button.h>
#include <gui/label.h>
#include <gui/text_field.h>
#include <gui/window.h>
#include <gui/list_data.h>
#include <gui/tileanim_frame.h>
#include <gui/seltile_swatch.h>
#include <functional>
#include <string_view>
#include <map>

void call_enemy_editor(int32_t index);

class EnemyEditorDialog: public GUI::Dialog<EnemyEditorDialog>
{
public:
	enum class message { OK, CANCEL, ENEMYCLASS };
	

	std::shared_ptr<GUI::Widget> view() override;
	bool handleMessage(const GUI::DialogMessage<message>& msg);

private:
	EnemyEditorDialog(guydata const& ref, char const* str, int32_t index);
	EnemyEditorDialog(int32_t index);
	void loadEnemyClass();
	void updateCSet(int32_t cset);
	std::shared_ptr<GUI::Window> window;
	std::shared_ptr<GUI::SelTileSwatch> oldtile, specialtile, newtile;
	std::shared_ptr<GUI::Checkbox> palbox;
	std::shared_ptr<GUI::TextField> paltext;
	std::shared_ptr<GUI::Label> l_attributes[32];
	std::shared_ptr<GUI::Button> ib_attributes[32];
	std::string h_attribute[32];
	PALETTE oldpal;
	std::string enemyname;
	int32_t index;
	GUI::ListData list_enemies;
	GUI::ListData list_weapons;
	GUI::ListData list_anim;
	GUI::ListData list_itemsets;
	guydata local_enemyref;
	friend void call_enemy_editor(int32_t index);
};

#endif