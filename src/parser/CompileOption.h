#ifndef ZSCRIPT_COMPILE_OPTION_H
#define ZSCRIPT_COMPILE_OPTION_H

#include <string>
#include "CompilerUtils.h"
#include "parserDefs.h"

namespace ZScript
{
	typedef int32_t CompileOptionValue;
	
	class CompileOptionSetting : public SafeBool<CompileOptionSetting>
	{
	public:
		static CompileOptionSetting Invalid;
		static CompileOptionSetting Default;
		static CompileOptionSetting Inherit;
		
		CompileOptionSetting();
		CompileOptionSetting(CompileOptionValue value);

		bool operator==(CompileOptionSetting const& rhs) const;

		bool safe_bool() const;
		
		// Get the set value if we're not a special type.
		optional<CompileOptionValue> getValue() const;

		std::string asString() const;

	private:
		enum Type
		{
			TYPE_UNSET,
			TYPE_DEFAULT,
			TYPE_INHERIT,
			TYPE_VALUE
		};

		Type type_;
		CompileOptionValue value_;

		CompileOptionSetting(Type type);
	};
	
	// This class has value semantics. Basically, just treat it like an
	// enum.
	class CompileOption
	{
	public:
		// Static instance for an invalid option.
		static CompileOption Invalid;

		// Declare static instance for each option.
#		define X(NAME, DEFAULTQR, TYPE, DEFAULTVAL) \
		static CompileOption OPT_##NAME;
#		include "CompileOption.xtable"
#		undef X

		static void initialize();
		static void updateDefaults();

		static optional<CompileOption> get(std::string const& name);

		CompileOption() : id_(-1) {}

		bool operator==(CompileOption const& rhs) const {
			return id_ == rhs.id_;}
		bool operator<(CompileOption const& rhs) const {
			return id_ < rhs.id_;}
		
		// Is this a valid option id?
		bool isValid() const;

		// Get the name of the option.
		std::string* getName() const;
	
		// Get the default option value.
		optional<CompileOptionValue> getDefault() const;
		void setDefault(CompileOptionValue value);
	
	private:
		int32_t id_;

		CompileOption(int32_t id) : id_(id) {}
	};
};

#endif
