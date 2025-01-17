#ifndef ZC_DIALOG_PICKRULETMP_H
#define ZC_DIALOG_PICKRULETMP_H

#include <gui/dialog.h>
#include "gui/checkbox.h"
#include "gui/label.h"
#include <functional>
#include <string_view>
#include "../zq_files.h"

void call_ruletemplate_dlg();

class PickRuleTemplateDialog: public GUI::Dialog<PickRuleTemplateDialog>
{
public:
	enum class message { OK, CANCEL };

	PickRuleTemplateDialog(std::function<void(int32_t)> setRuleTemplate);

	std::shared_ptr<GUI::Widget> view() override;
	bool handleMessage(const GUI::DialogMessage<message>& msg);

private:
	std::shared_ptr<GUI::Checkbox> templates[sz_ruletemplate];
	std::function<void(int32_t)> setRuleTemplate;
};

#endif
