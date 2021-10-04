#ifndef ZC_GUI_RADIO_H
#define ZC_GUI_RADIO_H

#include "widget.h"
#include "dialog_ref.h"

namespace GUI
{

class Radio: public Widget
{
public:
	Radio();

	/* Sets the text to appear next to the radio. */
	void setText(std::string newText);

	/* Sets whether the radio is checked or not. */
	void setChecked(bool value);

	/* Returns true if the radio is checked. */
	bool getChecked();

	void setProcSet(int newProcSet);
	
	int getProcSet() const {return procset;}
	
	void setIndex(size_t newIndex);
	
	size_t getIndex() const {return index;}

	template<typename T>
	RequireMessage<T> onToggle(T m)
	{
		message = static_cast<int>(m);
	}
protected:
	int message;
private:
	bool checked;
	std::string text;
	int procset;
	size_t index;
	DialogRef alDialog;

	void applyVisibility(bool visible) override;
	void realize(DialogRunner& runner) override;
	void calculateSize() override;
	int onEvent(int event, MessageDispatcher& sendMessage) override;
};

}

#endif