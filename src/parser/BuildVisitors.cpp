
#include "../precompiled.h" //always first
#include <assert.h>

#include "BuildVisitors.h"
#include "CompileError.h"
#include "Types.h"
#include "ZScript.h"

using namespace ZScript;
using std::map;
using std::pair;
using std::string;
using std::vector;
using std::list;
using std::shared_ptr;

/////////////////////////////////////////////////////////////////////////////////
// BuildOpcodes

BuildOpcodes::BuildOpcodes(Scope* curScope)
	: returnlabelid(-1), continuelabelids(), breaklabelids(), 
	  returnRefCount(0), continueRefCounts(), breakRefCounts()
{
	opcodeTargets.push_back(&result);
	scope = curScope;
}

void addOpcode2(vector<shared_ptr<Opcode>>& v, Opcode* code)
{
	shared_ptr<Opcode> op(code);
	v.push_back(op);
}

void BuildOpcodes::visit(AST& node, void* param)
{
	if(node.isDisabled()) return; //Don't visit disabled nodes.
	RecursiveVisitor::visit(node, param);
	for (vector<ASTExprConst*>::const_iterator it =
		     node.compileErrorCatches.begin();
		 it != node.compileErrorCatches.end(); ++it)
	{
		ASTExprConst& idNode = **it;
		optional<int32_t> errorId = idNode.getCompileTimeValue(NULL, scope);
		assert(errorId);
		handleError(CompileError::MissingCompileError(
				            &node, int32_t(*errorId / 10000L)));
	}
}

void BuildOpcodes::literalVisit(AST& node, void* param)
{
	if(node.isDisabled()) return; //Don't visit disabled nodes.
	int32_t initIndex = result.size();
	visit(node, param);
	//Handle literals
	OpcodeContext *c = (OpcodeContext *)param;
	result.insert(result.begin() + initIndex, c->initCode.begin(), c->initCode.end());
	c->initCode.clear();
}

void BuildOpcodes::literalVisit(AST* node, void* param)
{
	if(node) literalVisit(*node, param);
}

void BuildOpcodes::caseDefault(AST&, void*)
{
    // Unreachable
    assert(false);
}

void BuildOpcodes::addOpcode(Opcode* code)
{
	std::shared_ptr<Opcode> op(code);
	opcodeTargets.back()->push_back(op);
}

void BuildOpcodes::addOpcode(std::shared_ptr<Opcode> &code)
{
	opcodeTargets.back()->push_back(code);
}

template <class Container>
void BuildOpcodes::addOpcodes(Container const& container)
{
	for (auto ptr: container)
		addOpcode(ptr);
}

void BuildOpcodes::deallocateArrayRef(int32_t arrayRef)
{
	addOpcode(new OSetRegister(new VarArgument(SFTEMP), new VarArgument(SFRAME)));
	addOpcode(new OAddImmediate(new VarArgument(SFTEMP), new LiteralArgument(arrayRef)));
	addOpcode(new OLoadIndirect(new VarArgument(EXP2), new VarArgument(SFTEMP)));
	addOpcode(new ODeallocateMemRegister(new VarArgument(EXP2)));
}

void BuildOpcodes::deallocateRefsUntilCount(int32_t count)
{
	count = arrayRefs.size() - count;
    for (list<int32_t>::reverse_iterator it = arrayRefs.rbegin();
		 it != arrayRefs.rend() && count > 0;
		 it++, count--)
	{
		deallocateArrayRef(*it);
	}
}

void BuildOpcodes::caseSetOption(ASTSetOption&, void*)
{
	// Do nothing, not even recurse.
}

void BuildOpcodes::caseUsing(ASTUsingDecl& host, void*)
{
	// Do nothing, not even recurse.
}

// Statements

void BuildOpcodes::caseBlock(ASTBlock &host, void *param)
{
	if(!host.getScope())
	{
		host.setScope(scope->makeChild());
	}
	scope = host.getScope();
	
	OpcodeContext *c = (OpcodeContext *)param;

	int32_t startRefCount = arrayRefs.size();

    for (vector<ASTStmt*>::iterator it = host.statements.begin();
		 it != host.statements.end(); ++it)
	{
        literalVisit(*it, param);
	}

	deallocateRefsUntilCount(startRefCount);
	while ((int32_t)arrayRefs.size() > startRefCount)
		arrayRefs.pop_back();
	
	scope = scope->getParent();
}

void BuildOpcodes::caseStmtIf(ASTStmtIf &host, void *param)
{
	if(host.isDecl())
	{
		if(!host.getScope())
		{
			host.setScope(scope->makeChild());
		}
		scope = host.getScope();
		int32_t startRefCount = arrayRefs.size();
		
		if(optional<int32_t> val = host.declaration->getInitializer()->getCompileTimeValue(this, scope))
		{
			if((host.isInverted()) == (*val==0)) //True, so go straight to the 'then'
			{
				literalVisit(host.declaration.get(), param); 
				visit(host.thenStatement.get(), param);
				deallocateRefsUntilCount(startRefCount);
				
				while ((int32_t)arrayRefs.size() > startRefCount)
					arrayRefs.pop_back();
				
				scope = scope->getParent();
			} //Either true or false, it's constant, so no checks required.
			return;
		}
		
		int32_t endif = ScriptParser::getUniqueLabelID();
		literalVisit(host.declaration.get(), param);
		
		//The condition should be reading the value just processed from the initializer
		visit(host.condition.get(), param);
		//
		addOpcode(new OCompareImmediate(new VarArgument(EXP1), new LiteralArgument(0)));
		if(host.isInverted())
			addOpcode(new OGotoFalseImmediate(new LabelArgument(endif)));
		else
			addOpcode(new OGotoTrueImmediate(new LabelArgument(endif)));
		
		visit(host.thenStatement.get(), param);
		//nop
		Opcode *next = new ONoOp();
		next->setLabel(endif);
		addOpcode(next);
		
		deallocateRefsUntilCount(startRefCount);
		
		while ((int32_t)arrayRefs.size() > startRefCount)
			arrayRefs.pop_back();
		
		scope = scope->getParent();
	}
	else
	{
		if(optional<int32_t> val = host.condition->getCompileTimeValue(this, scope))
		{
			if((host.isInverted()) == (*val==0)) //True, so go straight to the 'then'
			{
				visit(host.thenStatement.get(), param);
			} //Either true or false, it's constant, so no checks required.
			return;
		}
		//run the test
		int32_t startRefCount = arrayRefs.size(); //Store ref count
		literalVisit(host.condition.get(), param);
		//Deallocate string/array literals from within the condition
		deallocateRefsUntilCount(startRefCount);
		while ((int32_t)arrayRefs.size() > startRefCount)
			arrayRefs.pop_back();
		//Continue
		int32_t endif = ScriptParser::getUniqueLabelID();
		addOpcode(new OCompareImmediate(new VarArgument(EXP1), new LiteralArgument(0)));
		if(host.isInverted())
			addOpcode(new OGotoFalseImmediate(new LabelArgument(endif)));
		else
			addOpcode(new OGotoTrueImmediate(new LabelArgument(endif)));
		//run the block
		visit(host.thenStatement.get(), param);
		//nop
		Opcode *next = new ONoOp();
		next->setLabel(endif);
		addOpcode(next);
	}
}

void BuildOpcodes::caseStmtIfElse(ASTStmtIfElse &host, void *param)
{
	if(host.isDecl())
	{
		if(!host.getScope())
		{
			host.setScope(scope->makeChild());
		}
		scope = host.getScope();
		int32_t startRefCount = arrayRefs.size();
		
		if(optional<int32_t> val = host.declaration->getInitializer()->getCompileTimeValue(this, scope))
		{
			if((host.isInverted()) == (*val==0)) //True, so go straight to the 'then'
			{
				literalVisit(host.declaration.get(), param); 
				visit(host.thenStatement.get(), param);
				//Deallocate after then block
				deallocateRefsUntilCount(startRefCount);
				
				while ((int32_t)arrayRefs.size() > startRefCount)
					arrayRefs.pop_back();
				
				scope = scope->getParent();
			}
			else //False, so go straight to the 'else'
			{
				//Deallocate before else block
				deallocateRefsUntilCount(startRefCount);
				
				while ((int32_t)arrayRefs.size() > startRefCount)
					arrayRefs.pop_back();
				
				scope = scope->getParent();
				//
				visit(host.elseStatement.get(), param);
			}
			//Either way, ignore the rest and return.
			return;
		}
		
		int32_t endif = ScriptParser::getUniqueLabelID();
		int32_t elseif = ScriptParser::getUniqueLabelID();
		literalVisit(host.declaration.get(), param);
		
		//The condition should be reading the value just processed from the initializer
		visit(host.condition.get(), param);
		//
		addOpcode(new OCompareImmediate(new VarArgument(EXP1), new LiteralArgument(0)));
		addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(1)));
		if(host.isInverted())
			addOpcode(new OGotoFalseImmediate(new LabelArgument(elseif)));
		else
			addOpcode(new OGotoTrueImmediate(new LabelArgument(elseif)));
		
		visit(host.thenStatement.get(), param);
		//nop
		addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(0)));
		Opcode *next = new OCompareImmediate(new VarArgument(EXP1), new LiteralArgument(0));
		next->setLabel(elseif);
		addOpcode(next);
		
		deallocateRefsUntilCount(startRefCount);
		
		while ((int32_t)arrayRefs.size() > startRefCount)
			arrayRefs.pop_back();
		
		scope = scope->getParent();
		
		addOpcode(new OGotoTrueImmediate(new LabelArgument(endif)));
		visit(host.elseStatement.get(), param);
		
		next = new ONoOp();
		next->setLabel(endif);
		addOpcode(next);
	}
	else
	{
		if(optional<int32_t> val = host.condition->getCompileTimeValue(this, scope))
		{
			if((host.isInverted()) == (*val==0)) //True, so go straight to the 'then'
			{
				visit(host.thenStatement.get(), param);
			}
			else //False, so go straight to the 'else'
			{
				visit(host.elseStatement.get(), param);
			}
			//Either way, ignore the rest and return.
			return;
		}
		//run the test
		int32_t startRefCount = arrayRefs.size(); //Store ref count
		literalVisit(host.condition.get(), param);
		//Deallocate string/array literals from within the condition
		deallocateRefsUntilCount(startRefCount);
		while ((int32_t)arrayRefs.size() > startRefCount)
			arrayRefs.pop_back();
		//Continue
		int32_t elseif = ScriptParser::getUniqueLabelID();
		int32_t endif = ScriptParser::getUniqueLabelID();
		addOpcode(new OCompareImmediate(new VarArgument(EXP1), new LiteralArgument(0)));
		if(host.isInverted())
			addOpcode(new OGotoFalseImmediate(new LabelArgument(elseif)));
		else
			addOpcode(new OGotoTrueImmediate(new LabelArgument(elseif)));
		//run if blocl
		visit(host.thenStatement.get(), param);
		addOpcode(new OGotoImmediate(new LabelArgument(endif)));
		Opcode *next = new ONoOp();
		next->setLabel(elseif);
		addOpcode(next);
		visit(host.elseStatement.get(), param);
		next = new ONoOp();
		next->setLabel(endif);
		addOpcode(next);
	}
}

void BuildOpcodes::caseStmtSwitch(ASTStmtSwitch &host, void* param)
{
	if(host.isString)
	{
		caseStmtStrSwitch(host, param);
		return;
	}
	
	map<ASTSwitchCases*, int32_t> labels;
	vector<ASTSwitchCases*> cases = host.cases.data();
	
	int32_t end_label = ScriptParser::getUniqueLabelID();;
	int32_t default_label = end_label;

	// save and override break label.
    breaklabelids.push_back(end_label);
	breakRefCounts.push_back(arrayRefs.size());

	// Evaluate the key.
	int32_t startRefCount = arrayRefs.size(); //Store ref count
	literalVisit(host.key.get(), param);
	//Deallocate string/array literals from within the key
	deallocateRefsUntilCount(startRefCount);
	while ((int32_t)arrayRefs.size() > startRefCount)
		arrayRefs.pop_back();
	
	if(cases.size() == 1 && cases.back()->isDefault) //Only default case
	{
		visit(cases.back()->block.get(), param);
		// Add ending label, for 'break;'
		std::shared_ptr<Opcode> next = std::make_shared<ONoOp>();
		next->setLabel(end_label);
		result.push_back(next);
		return;
	}
	
	//Continue
	
	addOpcode2(result, new OSetRegister(new VarArgument(SWITCHKEY), new VarArgument(EXP1)));

	// Add the tests and jumps.
	for (vector<ASTSwitchCases*>::iterator it = cases.begin(); it != cases.end(); ++it)
	{
		ASTSwitchCases* cases = *it;

		// Make the target label.
		int32_t label = ScriptParser::getUniqueLabelID();
		labels[cases] = label;

		// Run the tests for these cases.
		for (vector<ASTExprConst*>::iterator it = cases->cases.begin();
			 it != cases->cases.end();
			 ++it)
		{
			// Test this individual case.
			if(optional<int32_t> val = (*it)->getCompileTimeValue(this, scope))
			{
				addOpcode2(result, new OCompareImmediate(new VarArgument(SWITCHKEY), new LiteralArgument(*val)));
			}
			else //Shouldn't ever happen?
			{
				visit(*it, param);
				addOpcode2(result, new OCompareRegister(new VarArgument(SWITCHKEY), new VarArgument(EXP1)));
			}
			// If the test succeeds, jump to its label.
			addOpcode2(result, new OGotoTrueImmediate(new LabelArgument(label)));
		}
		for (vector<ASTRange*>::iterator it = cases->ranges.begin();
			it != cases->ranges.end();
			++it)
		{
			ASTRange& range = **it;
			int32_t skipLabel = ScriptParser::getUniqueLabelID();
			//Test each full range
			if(optional<int32_t> val = (*range.start).getCompileTimeValue(this, scope))  //Compare key to lower bound
			{
				addOpcode2(result, new OCompareImmediate(new VarArgument(SWITCHKEY), new LiteralArgument(*val)));
			}
			else //Shouldn't ever happen?
			{
				visit(*range.start, param);
				addOpcode2(result, new OCompareRegister(new VarArgument(SWITCHKEY), new VarArgument(EXP1)));
			}
			addOpcode2(result, new OSetMore(new VarArgument(EXP1))); //Set if key is IN the bound
			addOpcode2(result, new OCompareImmediate(new VarArgument(EXP1), new LiteralArgument(0))); //Compare if key is OUT of the bound
			addOpcode2(result, new OGotoTrueImmediate(new LabelArgument(skipLabel))); //Skip if key is OUT of the bound
			
			if(optional<int32_t> val = (*range.end).getCompileTimeValue(this, scope))  //Compare key to upper bound
			{
				addOpcode2(result, new OCompareImmediate(new VarArgument(SWITCHKEY), new LiteralArgument(*val)));
			}
			else //Shouldn't ever happen?
			{
				visit(*range.end, param);
				addOpcode2(result, new OCompareRegister(new VarArgument(SWITCHKEY), new VarArgument(EXP1)));
			}
			addOpcode2(result, new OSetLess(new VarArgument(EXP1)	)); //Set if key is IN the bound
			addOpcode2(result, new OCompareImmediate(new VarArgument(EXP1), new LiteralArgument(0))); //Compare if key is OUT of the bound
			addOpcode2(result, new OGotoFalseImmediate(new LabelArgument(label))); //If key is in bounds, jump to its label
			Opcode *end = new ONoOp(); //Just here so the skip label can be placed
			end->setLabel(skipLabel);
			addOpcode2(result, end); //add the skip label
		}

		// If this set includes the default case, mark it.
		if (cases->isDefault)
			default_label = label;
	}

	// Add direct jump to default case (or end if there isn't one.).
	addOpcode2(result, new OGotoImmediate(new LabelArgument(default_label)));

	// Add the actual code branches.
	for (vector<ASTSwitchCases*>::iterator it = cases.begin(); it != cases.end(); ++it)
	{
		ASTSwitchCases* cases = *it;

		// Mark start of the block we're adding.
		int32_t block_start_index = result.size();
		// Make a nop for starting the block.
		addOpcode2(result, new ONoOp());
		result[block_start_index]->setLabel(labels[cases]);
		// Add block.
		visit(cases->block.get(), param);
	}

	// Add ending label.
    Opcode *next = new ONoOp();
    next->setLabel(end_label);
	addOpcode2(result, next);

	// Restore break label.
    breaklabelids.pop_back();
    breakRefCounts.pop_back();
}

void BuildOpcodes::caseStmtStrSwitch(ASTStmtSwitch &host, void* param)
{
	map<ASTSwitchCases*, int32_t> labels;
	vector<ASTSwitchCases*> cases = host.cases.data();

	int32_t end_label = ScriptParser::getUniqueLabelID();;
	int32_t default_label = end_label;

	// save and override break label.
    breaklabelids.push_back(end_label);
	breakRefCounts.push_back(arrayRefs.size());

	// Evaluate the key.
	int32_t startRefCount = arrayRefs.size(); //Store ref count
	literalVisit(host.key.get(), param);
	//Deallocate string/array literals from within the key
	deallocateRefsUntilCount(startRefCount);
	while ((int32_t)arrayRefs.size() > startRefCount)
		arrayRefs.pop_back();
	
	if(cases.size() == 1 && cases.back()->isDefault) //Only default case
	{
		visit(cases.back()->block.get(), param);
		// Add ending label, for 'break;'
		Opcode *next = new ONoOp();
		next->setLabel(end_label);
		addOpcode2(result, next);
		return;
	}
	
	//Continue
	addOpcode2(result, new OSetRegister(new VarArgument(SWITCHKEY), new VarArgument(EXP1)));

	// Add the tests and jumps.
	for (vector<ASTSwitchCases*>::iterator it = cases.begin(); it != cases.end(); ++it)
	{
		ASTSwitchCases* cases = *it;

		// Make the target label.
		int32_t label = ScriptParser::getUniqueLabelID();
		labels[cases] = label;

		// Run the tests for these cases.
		for (vector<ASTStringLiteral*>::iterator it = cases->str_cases.begin();
			 it != cases->str_cases.end();
			 ++it)
		{
			// Test this individual case.
			//Allocate the string literal
			int32_t litRefCount = arrayRefs.size(); //Store ref count
			literalVisit(*it, param);
			
			// Compare the strings
			if(*lookupOption(*scope, CompileOption::OPT_STRING_SWITCH_CASE_INSENSITIVE))
				addOpcode2(result, new OInternalInsensitiveStringCompare(new VarArgument(SWITCHKEY), new VarArgument(EXP1)));
			else
				addOpcode2(result, new OInternalStringCompare(new VarArgument(SWITCHKEY), new VarArgument(EXP1)));
			
			//Deallocate string literal
			deallocateRefsUntilCount(litRefCount);
			while ((int32_t)arrayRefs.size() > litRefCount)
				arrayRefs.pop_back();
			
			//
			addOpcode2(result, new OGotoTrueImmediate(new LabelArgument(label)));
		}

		// If this set includes the default case, mark it.
		if (cases->isDefault)
			default_label = label;
	}

	// Add direct jump to default case (or end if there isn't one.).
	addOpcode2(result, new OGotoImmediate(new LabelArgument(default_label)));

	// Add the actual code branches.
	for (vector<ASTSwitchCases*>::iterator it = cases.begin(); it != cases.end(); ++it)
	{
		ASTSwitchCases* cases = *it;

		// Mark start of the block we're adding.
		int32_t block_start_index = result.size();
		// Make a nop for starting the block.
		addOpcode2(result, new ONoOp());
		result[block_start_index]->setLabel(labels[cases]);
		// Add block.
		visit(cases->block.get(), param);
	}

	// Add ending label.
    Opcode *next = new ONoOp();
    next->setLabel(end_label);
	addOpcode2(result, next);

	// Restore break label.
    breaklabelids.pop_back();
    breakRefCounts.pop_back();
}

void BuildOpcodes::caseStmtFor(ASTStmtFor &host, void *param)
{
	if(!host.getScope())
	{
		host.setScope(scope->makeChild());
	}
	scope = host.getScope();
    //run the precondition
	int32_t setupRefCount = arrayRefs.size(); //Store ref count
	literalVisit(host.setup.get(), param);
	//Deallocate string/array literals from within the setup
	deallocateRefsUntilCount(setupRefCount);
	while ((int32_t)arrayRefs.size() > setupRefCount)
		arrayRefs.pop_back();
	//Check for a constant FALSE condition
	if(optional<int32_t> val = host.test->getCompileTimeValue(this, scope))
	{
		if(*val == 0) //False, so restore scope and exit
		{
			scope = scope->getParent();
			return;
		}
	}
	//Continue
    int32_t loopstart = ScriptParser::getUniqueLabelID();
    int32_t loopend = ScriptParser::getUniqueLabelID();
    int32_t loopincr = ScriptParser::getUniqueLabelID();
    //nop
    Opcode *next = new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(0));
    next->setLabel(loopstart);
    addOpcode(next);
    //test the termination condition
    int32_t testRefCount = arrayRefs.size(); //Store ref count
	literalVisit(host.test.get(), param);
	//Deallocate string/array literals from within the test
	deallocateRefsUntilCount(testRefCount);
	while ((int32_t)arrayRefs.size() > testRefCount)
		arrayRefs.pop_back();
	//Continue
    addOpcode(new OCompareImmediate(new VarArgument(EXP1), new LiteralArgument(0)));
    addOpcode(new OGotoTrueImmediate(new LabelArgument(loopend)));
    //run the loop body
    //save the old break and continue values

    breaklabelids.push_back(loopend);
	breakRefCounts.push_back(arrayRefs.size());
    continuelabelids.push_back(loopincr);
	continueRefCounts.push_back(arrayRefs.size());

	visit(host.body.get(), param);

    breaklabelids.pop_back();
    continuelabelids.pop_back();
    breakRefCounts.pop_back();
	continueRefCounts.pop_back();

    //run the increment
    //nop
    next = new OSetImmediate(new VarArgument(EXP2), new LiteralArgument(0));
    next->setLabel(loopincr);
    addOpcode(next);
    int32_t incRefCount = arrayRefs.size(); //Store ref count
	literalVisit(host.increment.get(), param);
	//Deallocate string/array literals from within the increment
	deallocateRefsUntilCount(incRefCount);
	while ((int32_t)arrayRefs.size() > incRefCount)
		arrayRefs.pop_back();
	//Continue
    addOpcode(new OGotoImmediate(new LabelArgument(loopstart)));
    //nop
    next = new OSetImmediate(new VarArgument(EXP2), new LiteralArgument(0));
    next->setLabel(loopend);
    addOpcode(next);
	scope = scope->getParent();
}

void BuildOpcodes::caseStmtWhile(ASTStmtWhile &host, void *param)
{
	if(optional<int32_t> val = host.test->getCompileTimeValue(this, scope))
	{
		if((host.isInverted()) != (*val==0)) //False, so ignore this all.
		{
			return;
		}
	}
    int32_t startlabel = ScriptParser::getUniqueLabelID();
    int32_t endlabel = ScriptParser::getUniqueLabelID();
    //run the test
    //nop to label start
    Opcode *start = new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(0));
    start->setLabel(startlabel);
    addOpcode(start);
	int32_t startRefCount = arrayRefs.size(); //Store ref count
	literalVisit(host.test.get(), param);
	//Deallocate string/array literals from within the test
	deallocateRefsUntilCount(startRefCount);
	while ((int32_t)arrayRefs.size() > startRefCount)
		arrayRefs.pop_back();
	//Continue
    addOpcode(new OCompareImmediate(new VarArgument(EXP1), new LiteralArgument(0)));
    if(host.isInverted()) //Is this `until` or `while`?
	{
		addOpcode(new OGotoFalseImmediate(new LabelArgument(endlabel)));
	}
	else
	{
		addOpcode(new OGotoTrueImmediate(new LabelArgument(endlabel)));
	}

    breaklabelids.push_back(endlabel);
	breakRefCounts.push_back(arrayRefs.size());
    continuelabelids.push_back(startlabel);
	continueRefCounts.push_back(arrayRefs.size());

	visit(host.body.get(), param);

    breaklabelids.pop_back();
    continuelabelids.pop_back();
    breakRefCounts.pop_back();
	continueRefCounts.pop_back();

    addOpcode(new OGotoImmediate(new LabelArgument(startlabel)));
    //nop to end while
    Opcode *end = new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(0));
    end->setLabel(endlabel);
    addOpcode(end);
}

void BuildOpcodes::caseStmtDo(ASTStmtDo &host, void *param)
{
    int32_t startlabel = ScriptParser::getUniqueLabelID();
    int32_t endlabel = ScriptParser::getUniqueLabelID();
    int32_t continuelabel = ScriptParser::getUniqueLabelID();
    //nop to label start
    Opcode *start = new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(0));
    start->setLabel(startlabel);
    addOpcode(start);

    breaklabelids.push_back(endlabel);
	breakRefCounts.push_back(arrayRefs.size());
    continuelabelids.push_back(continuelabel);
	continueRefCounts.push_back(arrayRefs.size());

	visit(host.body.get(), param);

    breaklabelids.pop_back();
    continuelabelids.pop_back();
    breakRefCounts.pop_back();
	continueRefCounts.pop_back();

    start = new OSetImmediate(new VarArgument(NUL), new LiteralArgument(0));
    start->setLabel(continuelabel);
    addOpcode(start);
    int32_t startRefCount = arrayRefs.size(); //Store ref count
	literalVisit(host.test.get(), param);
	//Deallocate string/array literals from within the test
	deallocateRefsUntilCount(startRefCount);
	while ((int32_t)arrayRefs.size() > startRefCount)
		arrayRefs.pop_back();
	//Continue
    addOpcode(new OCompareImmediate(new VarArgument(EXP1), new LiteralArgument(0)));
    if(host.isInverted()) //Is this `until` or `while`?
	{
		addOpcode(new OGotoFalseImmediate(new LabelArgument(endlabel)));
	}
	else
	{
		addOpcode(new OGotoTrueImmediate(new LabelArgument(endlabel)));
	}
    addOpcode(new OGotoImmediate(new LabelArgument(startlabel)));
    //nop to end dowhile
    Opcode *end = new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(0));
    end->setLabel(endlabel);
    addOpcode(end);
}

void BuildOpcodes::caseStmtReturn(ASTStmtReturn&, void*)
{
	deallocateRefsUntilCount(0);
	addOpcode(new OGotoImmediate(new LabelArgument(returnlabelid)));
}

void BuildOpcodes::caseStmtReturnVal(ASTStmtReturnVal &host, void *param)
{
	visit(host.value.get(), param);
	deallocateRefsUntilCount(0);
	addOpcode(new OGotoImmediate(new LabelArgument(returnlabelid)));
}

void BuildOpcodes::caseStmtBreak(ASTStmtBreak &host, void *)
{
	if(!host.breakCount) return;
    if (breaklabelids.size() < host.breakCount)
    {
	    handleError(CompileError::BreakBad(&host,host.breakCount));
        return;
    }
	int32_t refcount = breakRefCounts.at(breakRefCounts.size()-host.breakCount);
	int32_t breaklabel = breaklabelids.at(breaklabelids.size()-host.breakCount);
	deallocateRefsUntilCount(refcount);
    addOpcode(new OGotoImmediate(new LabelArgument(breaklabel)));
}

void BuildOpcodes::caseStmtContinue(ASTStmtContinue &host, void *)
{
	if(!host.contCount) return;
    if (continuelabelids.size() < host.contCount)
    {
	    handleError(CompileError::ContinueBad(&host,host.contCount));
        return;
    }

	int32_t refcount = continueRefCounts.at(continueRefCounts.size()-host.contCount);
	int32_t contlabel = continuelabelids.at(continuelabelids.size()-host.contCount);
	deallocateRefsUntilCount(refcount);
    addOpcode(new OGotoImmediate(new LabelArgument(contlabel)));
}

void BuildOpcodes::caseStmtEmpty(ASTStmtEmpty &, void *)
{
    // empty
}

// Declarations

void BuildOpcodes::caseFuncDecl(ASTFuncDecl &host, void *param)
{
	if(host.getFlag(FUNCFLAG_INLINE)) return; //Skip inline func decls, they are handled at call location -V
	if(host.prototype) return; //Same for prototypes
	int32_t oldreturnlabelid = returnlabelid;
	int32_t oldReturnRefCount = returnRefCount;
    returnlabelid = ScriptParser::getUniqueLabelID();
	returnRefCount = arrayRefs.size();

	visit(host.block.get(), param);
}

void BuildOpcodes::caseDataDecl(ASTDataDecl& host, void* param)
{
    OpcodeContext& context = *(OpcodeContext*)param;
	Datum& manager = *host.manager;
	ASTExpr* init = host.getInitializer();

	// Ignore inlined values.
	if (manager.getCompileTimeValue()) return;

	// Switch off to the proper helper function.
	if (manager.type.isArray()
	    || (init && (init->isArrayLiteral()
	                 || init->isStringLiteral())))
	{
		if (init) buildArrayInit(host, context);
		else buildArrayUninit(host, context);
	}
	else buildVariable(host, context);
}

void BuildOpcodes::buildVariable(ASTDataDecl& host, OpcodeContext& context)
{
	Datum& manager = *host.manager;

	// Load initializer into EXP1, if present.
	visit(host.getInitializer(), &context);

	// Set variable to EXP1 or 0, depending on the initializer.
	if (optional<int32_t> globalId = manager.getGlobalId())
	{
		if (host.getInitializer())
			addOpcode(new OSetRegister(new GlobalArgument(*globalId),
			                           new VarArgument(EXP1)));
		else
			addOpcode(new OSetImmediate(new GlobalArgument(*globalId),
			                            new LiteralArgument(0)));
	}
	else
	{
		int32_t offset = 10000L * *getStackOffset(manager);
		addOpcode(new OSetRegister(new VarArgument(SFTEMP), new VarArgument(SFRAME)));
		addOpcode(new OAddImmediate(new VarArgument(SFTEMP), new LiteralArgument(offset)));
		if (!host.getInitializer())
			addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(0)));
		addOpcode(new OStoreIndirect(new VarArgument(EXP1), new VarArgument(SFTEMP)));
	}
}

void BuildOpcodes::buildArrayInit(ASTDataDecl& host, OpcodeContext& context)
{
	Datum& manager = *host.manager;

	// Initializer should take care of everything.
	visit(host.getInitializer(), &context);
}

void BuildOpcodes::buildArrayUninit(
		ASTDataDecl& host, OpcodeContext& context)
{
	Datum& manager = *host.manager;

	// Right now, don't support nested arrays.
	if (host.extraArrays.size() != 1)
	{
		handleError(CompileError::DimensionMismatch(&host));
		return;
	}

	// Get size of the array.
	int32_t totalSize;
	if (optional<int32_t> size = host.extraArrays[0]->getCompileTimeSize(this, scope))
		totalSize = *size * 10000L;
	else
	{
		handleError(
				CompileError::ExprNotConstant(host.extraArrays[0]));
		return;
	}

	// Allocate the array.
	if (optional<int32_t> globalId = manager.getGlobalId())
	{
		addOpcode(new OAllocateGlobalMemImmediate(
				          new VarArgument(EXP1),
				          new LiteralArgument(totalSize)));
		addOpcode(new OSetRegister(new GlobalArgument(*globalId),
		                           new VarArgument(EXP1)));
	}
	else
	{
		addOpcode(new OAllocateMemImmediate(new VarArgument(EXP1), new LiteralArgument(totalSize)));
		int32_t offset = 10000L * *getStackOffset(manager);
		addOpcode(new OSetRegister(new VarArgument(SFTEMP), new VarArgument(SFRAME)));
		addOpcode(new OAddImmediate(new VarArgument(SFTEMP), new LiteralArgument(offset)));
		addOpcode(new OStoreIndirect(new VarArgument(EXP1), new VarArgument(SFTEMP)));
		// Register for cleanup.
		arrayRefs.push_back(offset);
	}
}

void BuildOpcodes::caseDataTypeDef(ASTDataTypeDef&, void*) {}

void BuildOpcodes::caseCustomDataTypeDef(ASTDataTypeDef&, void*) {}

// Expressions

void BuildOpcodes::caseExprAssign(ASTExprAssign &host, void *param)
{
    //load the rval into EXP1
	visit(host.right.get(), param);
    //and store it
    LValBOHelper helper(scope);
    host.left->execute(helper, param);
	addOpcodes(helper.getResult());
}

void BuildOpcodes::caseExprIdentifier(ASTExprIdentifier& host, void* param)
{
    OpcodeContext* c = (OpcodeContext*)param;

	// If a constant, just load its value.
    if (optional<int32_t> value = host.binding->getCompileTimeValue(scope->isGlobal() || scope->isScript()))
    {
        addOpcode(new OSetImmediate(new VarArgument(EXP1),
                                    new LiteralArgument(*value)));
		host.markConstant();
        return;
    }

    int32_t vid = host.binding->id;

    if (optional<int32_t> globalId = host.binding->getGlobalId())
    {
        // Global variable, so just get its value.
        addOpcode(new OSetRegister(new VarArgument(EXP1),
                                   new GlobalArgument(*globalId)));
        return;
    }

    // Local variable, get its value from the stack.
    int32_t offset = 10000L * *getStackOffset(*host.binding);
    addOpcode(new OSetRegister(new VarArgument(SFTEMP), new VarArgument(SFRAME)));
    addOpcode(new OAddImmediate(new VarArgument(SFTEMP), new LiteralArgument(offset)));
    addOpcode(new OLoadIndirect(new VarArgument(EXP1), new VarArgument(SFTEMP)));
}

void BuildOpcodes::caseExprArrow(ASTExprArrow& host, void* param)
{
    OpcodeContext *c = (OpcodeContext *)param;
    int32_t isIndexed = (host.index != NULL);
	assert(host.readFunction->isInternal());
	
	if(host.readFunction->getFlag(FUNCFLAG_INLINE))
	{
		if (!(host.readFunction->internal_flags & IFUNCFLAG_SKIPPOINTER))
		{
			//push the lhs of the arrow
			visit(host.left.get(), param);
			addOpcode(new OPushRegister(new VarArgument(EXP1)));
		}
		
		if(isIndexed)
		{
			visit(host.index.get(), param);
			addOpcode(new OPushRegister(new VarArgument(EXP1)));
		}
		
		std::vector<std::shared_ptr<Opcode>> const& funcCode = host.readFunction->getCode();
		for(auto it = funcCode.begin();
			it != funcCode.end(); ++it)
		{
			addOpcode((*it)->makeClone());
		}
	}
	else
	{
		//this is 	actually a function call
		//to the appropriate gettor method
		//so, set that up:
		//push the stack frame
		addOpcode(new OPushRegister(new VarArgument(SFRAME)));
		int32_t returnlabel = ScriptParser::getUniqueLabelID();
		//push the return address
		addOpcode(new OPushImmediate(new LabelArgument(returnlabel)));
		if (!(host.readFunction->internal_flags & IFUNCFLAG_SKIPPOINTER))
		{
			//push the lhs of the arrow
			visit(host.left.get(), param);
			addOpcode(new OPushRegister(new VarArgument(EXP1)));
		}

		//if indexed, push the index
		if(isIndexed)
		{
			visit(host.index.get(), param);
			addOpcode(new OPushRegister(new VarArgument(EXP1)));
		}

		//call the function
		int32_t label = host.readFunction->getLabel();
		addOpcode(new OGotoImmediate(new LabelArgument(label)));
		//pop the stack frame
		Opcode *next = new OPopRegister(new VarArgument(SFRAME));
		next->setLabel(returnlabel);
		addOpcode(next);
	}
}

void BuildOpcodes::caseExprIndex(ASTExprIndex& host, void* param)
{
	// If the left hand side is an arrow, then we'll let it run instead.
	if (host.array->isTypeArrow())
	{
		caseExprArrow(static_cast<ASTExprArrow&>(*host.array), param);
		return;
	}
	optional<int32_t> arrVal = host.array->getCompileTimeValue(this,scope);
	optional<int32_t> indxVal = host.index->getCompileTimeValue(this,scope);
	
	if(!arrVal)
	{
		// First, push the array.
		visit(host.array.get(), param);
		addOpcode(new OPushRegister(new VarArgument(EXP1)));
	}

	if(!indxVal)
	{
		//Load the index
		visit(host.index.get(), param);
	}
	// Pop array into INDEX.
	if(arrVal) addOpcode(new OSetImmediate(new VarArgument(INDEX), new LiteralArgument(*arrVal)));
	else addOpcode(new OPopRegister(new VarArgument(INDEX)));
	
	if(indxVal) addOpcode(new OReadPODArrayI(new VarArgument(EXP1), new LiteralArgument(*indxVal)));
	else addOpcode(new OReadPODArrayR(new VarArgument(EXP1), new VarArgument(EXP1)));
}

void BuildOpcodes::caseExprCall(ASTExprCall& host, void* param)
{
	if (host.isDisabled()) return;
    OpcodeContext* c = (OpcodeContext*)param;
	if(host.binding->prototype) //Prototype function
	{
		int32_t startRefCount = arrayRefs.size(); //Store ref count
		//Visit each parameter, in case there are side-effects; but don't push the results, as they are unneeded.
		for (vector<ASTExpr*>::iterator it = host.parameters.begin();
			it != host.parameters.end(); ++it)
		{
			visit(*it, param);
		}
		
		//Set the return to the default value
		DataType const& retType = *host.binding->returnType;
		if(retType != DataType::ZVOID)
		{
			if (optional<int32_t> val = host.binding->defaultReturn->getCompileTimeValue(NULL, scope))
			{
				addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*val)));
			}
		}

		//Deallocate string/array literals from within the parameters
		deallocateRefsUntilCount(startRefCount);
		while ((int32_t)arrayRefs.size() > startRefCount)
			arrayRefs.pop_back();
	}
	else if(host.binding->getFlag(FUNCFLAG_INLINE)) //Inline function
	{
		if(host.binding->isInternal())
		{
			int32_t startRefCount = arrayRefs.size(); //Store ref count
			
			if (host.left->isTypeArrow() && !(host.binding->internal_flags & IFUNCFLAG_SKIPPOINTER))
			{
				//load the value of the left-hand of the arrow into EXP1
				visit(static_cast<ASTExprArrow&>(*host.left).left.get(), param);
				//visit(host.getLeft(), param);
				//push it onto the stack
				addOpcode(new OPushRegister(new VarArgument(EXP1)));
			}
			//push the parameters, in forward order
			for (vector<ASTExpr*>::iterator it = host.parameters.begin();
				it != host.parameters.end(); ++it)
			{
				visit(*it, param);
				addOpcode(new OPushRegister(new VarArgument(EXP1)));
			}
			
			std::vector<std::shared_ptr<Opcode>> const& funcCode = host.binding->getCode();
			for(auto it = funcCode.begin();
				it != funcCode.end(); ++it)
			{
				addOpcode((*it)->makeClone());
			}
		
			if(host.left->isTypeArrow())
			{
				ASTExprArrow* arr = static_cast<ASTExprArrow*>(host.left.get());
				if(arr->left->getWriteType(scope, this) && !arr->left->isConstant())
				{
					if(host.binding->internal_flags & IFUNCFLAG_REASSIGNPTR)
					{
						bool isVoid = host.binding->returnType->isVoid();
						if(!isVoid) addOpcode(new OPushRegister(new VarArgument(EXP1)));
						addOpcode(new OSetRegister(new VarArgument(EXP1), new VarArgument(EXP2)));
						LValBOHelper helper(scope);
						arr->left->execute(helper, param);
						addOpcodes(helper.getResult());
						if(!isVoid) addOpcode(new OPopRegister(new VarArgument(EXP1)));
					}
				}
				else if(host.binding->internal_flags & IFUNCFLAG_REASSIGNPTR) //This is likely a mistake in the script... give the user a warning.
				{
					handleError(CompileError::BadReassignCall(&host, host.binding->getSignature().asString()));
				}
			}
			//Deallocate string/array literals from within the parameters
			deallocateRefsUntilCount(startRefCount);
			while ((int32_t)arrayRefs.size() > startRefCount)
				arrayRefs.pop_back();
		}
		else
		{
			/* This section has issues, and a totally new system for parameters must be devised. For now, just disabling inlining of user functions altogether. -V
												
			// If the function is a pointer function (->func()) we need to push the
			// left-hand-side.
			if (host.left->isTypeArrow() && !(host.binding->internal_flags & IFUNCFLAG_SKIPPOINTER))
			{
				//load the value of the left-hand of the arrow into EXP1
				visit(static_cast<ASTExprArrow&>(*host.left).left.get(), param);
				//visit(host.getLeft(), param);
				//push it onto the stack
				addOpcode(new OPushRegister(new VarArgument(EXP1)));
			}
			//push the data decls, in forward order
			for (vector<ASTDataDecl*>::iterator it = host.inlineParams.begin();
				it != host.inlineParams.end(); ++it)
			{
				visit(*it, param);
			}
			
			//Inline-specific:
			ASTFuncDecl& decl = *(host.binding->node);
			//Set the inline flag, process the function block, then reset flags to prior state.
			int32_t oldreturnlabelid = returnlabelid;
			int32_t oldReturnRefCount = returnRefCount;
			returnlabelid = ScriptParser::getUniqueLabelID();
			returnRefCount = arrayRefs.size();
			
			visit(*host.inlineBlock, param);
			
			Opcode *next = new ONoOp(); //Just here so the label can be placed.
			next->setLabel(returnlabelid);
			addOpcode(next);
			
			returnlabelid = oldreturnlabelid;
			returnRefCount = oldReturnRefCount;*/
		}
	}
	else //Non-inline function
	{
		int32_t funclabel = host.binding->getLabel();
		//push the stack frame pointer
		addOpcode(new OPushRegister(new VarArgument(SFRAME)));
		//push the return address
		int32_t returnaddr = ScriptParser::getUniqueLabelID();
		int32_t startRefCount = arrayRefs.size(); //Store ref count
		//addOpcode(new OSetImmediate(new VarArgument(EXP1), new LabelArgument(returnaddr)));
		//addOpcode(new OPushRegister(new VarArgument(EXP1)));
		addOpcode(new OPushImmediate(new LabelArgument(returnaddr)));
		
		
		// If the function is a pointer function (->func()) we need to push the
		// left-hand-side.
		if (host.left->isTypeArrow() && !(host.binding->internal_flags & IFUNCFLAG_SKIPPOINTER))
		{
			//load the value of the left-hand of the arrow into EXP1
			visit(static_cast<ASTExprArrow&>(*host.left).left.get(), param);
			//visit(host.getLeft(), param);
			//push it onto the stack
			addOpcode(new OPushRegister(new VarArgument(EXP1)));
		}

		//push the parameters, in forward order
		for (vector<ASTExpr*>::iterator it = host.parameters.begin();
			it != host.parameters.end(); ++it)
		{
			visit(*it, param);
			addOpcode(new OPushRegister(new VarArgument(EXP1)));
		}
		//goto
		addOpcode(new OGotoImmediate(new LabelArgument(funclabel)));
		//pop the stack frame pointer
		Opcode *next = new OPopRegister(new VarArgument(SFRAME));
		next->setLabel(returnaddr);
		addOpcode(next);
		
		if(host.left->isTypeArrow())
		{
			ASTExprArrow* arr = static_cast<ASTExprArrow*>(host.left.get());
			if(arr->left->getWriteType(scope, this) && !arr->left->isConstant())
			{
				if(host.binding->internal_flags & IFUNCFLAG_REASSIGNPTR)
				{
					bool isVoid = host.binding->returnType->isVoid();
					if(!isVoid) addOpcode(new OPushRegister(new VarArgument(EXP1)));
					addOpcode(new OSetRegister(new VarArgument(EXP1), new VarArgument(EXP2)));
					LValBOHelper helper(scope);
					arr->left->execute(helper, param);
					addOpcodes(helper.getResult());
					if(!isVoid) addOpcode(new OPopRegister(new VarArgument(EXP1)));
				}
			}
			else if(host.binding->internal_flags & IFUNCFLAG_REASSIGNPTR) //This is likely a mistake in the script... give the user a warning.
			{
				handleError(CompileError::BadReassignCall(&host, host.binding->getSignature().asString()));
			}
		}
		
		//Deallocate string/array literals from within the parameters
		deallocateRefsUntilCount(startRefCount);
		while ((int32_t)arrayRefs.size() > startRefCount)
		arrayRefs.pop_back();
	}
}

void BuildOpcodes::caseExprNegate(ASTExprNegate& host, void* param)
{
    if (optional<int32_t> val = host.getCompileTimeValue(NULL, scope))
    {
        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*val)));
        return;
    }

    visit(host.operand.get(), param);
    addOpcode(new OSetImmediate(new VarArgument(EXP2), new LiteralArgument(0)));
    addOpcode(new OSubRegister(new VarArgument(EXP2), new VarArgument(EXP1)));
    addOpcode(new OSetRegister(new VarArgument(EXP1), new VarArgument(EXP2)));
}

void BuildOpcodes::caseExprNot(ASTExprNot& host, void* param)
{
    if (host.getCompileTimeValue(NULL, scope))
    {
        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
        return;
    }

    visit(host.operand.get(), param);
    addOpcode(new OCompareImmediate(new VarArgument(EXP1), new LiteralArgument(0)));
	if(*lookupOption(*scope, CompileOption::OPT_BOOL_TRUE_RETURN_DECIMAL))
		addOpcode(new OSetTrue(new VarArgument(EXP1)));
	else
		addOpcode(new OSetTrueI(new VarArgument(EXP1)));
}

void BuildOpcodes::caseExprBitNot(ASTExprBitNot& host, void* param)
{
    if (host.getCompileTimeValue(NULL, scope))
    {
        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
        return;
    }

    visit(host.operand.get(), param);
	
	if(*lookupOption(*scope, CompileOption::OPT_BINARY_32BIT)
	   || host.operand.get()->isLong(scope, this))
		addOpcode(new O32BitNot(new VarArgument(EXP1)));
	else
		addOpcode(new ONot(new VarArgument(EXP1)));
}

void BuildOpcodes::caseExprIncrement(ASTExprIncrement& host, void* param)
{
    OpcodeContext* c = (OpcodeContext*)param;

    // Load value of the variable into EXP1 and push.
    visit(host.operand.get(), param);
    addOpcode(new OPushRegister(new VarArgument(EXP1)));

    // Increment EXP1
    addOpcode(new OAddImmediate(new VarArgument(EXP1),
								new LiteralArgument(10000)));
	
    // Store it
    LValBOHelper helper(scope);
    host.operand->execute(helper, param);
    addOpcodes(helper.getResult());
	
    // Pop EXP1
    addOpcode(new OPopRegister(new VarArgument(EXP1)));
}

void BuildOpcodes::caseExprPreIncrement(ASTExprPreIncrement& host, void* param)
{
    OpcodeContext* c = (OpcodeContext*)param;

    // Load value of the variable into EXP1.
    visit(host.operand.get(), param);

    // Increment EXP1
    addOpcode(new OAddImmediate(new VarArgument(EXP1), new LiteralArgument(10000)));

    // Store it
    LValBOHelper helper(scope);
    host.operand->execute(helper, param);
	addOpcodes(helper.getResult());
}

void BuildOpcodes::caseExprPreDecrement(ASTExprPreDecrement& host, void* param)
{
    OpcodeContext* c = (OpcodeContext*)param;

    // Load value of the variable into EXP1.
	visit(host.operand.get(), param);

    // Decrement EXP1.
    addOpcode(new OSubImmediate(new VarArgument(EXP1),
								new LiteralArgument(10000)));

    // Store it.
    LValBOHelper helper(scope);
    host.operand->execute(helper, param);
	addOpcodes(helper.getResult());
}

void BuildOpcodes::caseExprDecrement(ASTExprDecrement& host, void* param)
{
    OpcodeContext* c = (OpcodeContext*)param;

    // Load value of the variable into EXP1 and push.
	visit(host.operand.get(), param);
    addOpcode(new OPushRegister(new VarArgument(EXP1)));

    // Decrement EXP1.
    addOpcode(new OSubImmediate(new VarArgument(EXP1),
								new LiteralArgument(10000)));
    // Store it.
    LValBOHelper helper(scope);
    host.operand->execute(helper, param);
	addOpcodes(helper.getResult());

    // Pop EXP1.
    addOpcode(new OPopRegister(new VarArgument(EXP1)));
}

void BuildOpcodes::caseExprAnd(ASTExprAnd& host, void* param)
{
    if (host.getCompileTimeValue(NULL, scope))
    {
        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
        return;
    }
	bool short_circuit = *lookupOption(*scope, CompileOption::OPT_SHORT_CIRCUIT) != 0;
    if(short_circuit)
	{
		int32_t skip = ScriptParser::getUniqueLabelID();
		//Get left
		visit(host.left.get(), param);
		//Check left, skip if false
		addOpcode(new OCompareImmediate(new VarArgument(EXP1), new LiteralArgument(0)));
		addOpcode(new OGotoTrueImmediate(new LabelArgument(skip)));
		//Get right
		visit(host.right.get(), param);
		addOpcode(new OCastBoolF(new VarArgument(EXP1))); //Don't break boolean ops on negative numbers on the RHS.
		Opcode* ocode =  new OCompareImmediate(new VarArgument(EXP1), new LiteralArgument(1));
		ocode->setLabel(skip);
		addOpcode(ocode);
	}
	else
	{
		//Get left
		visit(host.left.get(), param);
		//Store left for later
		addOpcode(new OPushRegister(new VarArgument(EXP1)));
		//Get right
		visit(host.right.get(), param);
		//Retrieve left
		addOpcode(new OPopRegister(new VarArgument(EXP2)));
		addOpcode(new OCastBoolF(new VarArgument(EXP1)));
		addOpcode(new OCastBoolF(new VarArgument(EXP2)));
		addOpcode(new OAddRegister(new VarArgument(EXP1), new VarArgument(EXP2)));
		addOpcode(new OCompareImmediate(new VarArgument(EXP1), new LiteralArgument(2)));
	}
	if(*lookupOption(*scope, CompileOption::OPT_BOOL_TRUE_RETURN_DECIMAL))
		addOpcode(new OSetMore(new VarArgument(EXP1)));
	else
		addOpcode(new OSetMoreI(new VarArgument(EXP1)));
}

void BuildOpcodes::caseExprOr(ASTExprOr& host, void* param)
{
    if (host.getCompileTimeValue(NULL, scope))
    {
        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
        return;
    }
	bool short_circuit = *lookupOption(*scope, CompileOption::OPT_SHORT_CIRCUIT) != 0;
	if(short_circuit)
	{
		int32_t skip = ScriptParser::getUniqueLabelID();
		//Get left
		visit(host.left.get(), param);
		//Check left, skip if true
		addOpcode(new OCompareImmediate(new VarArgument(EXP1), new LiteralArgument(1)));
		addOpcode(new OGotoMoreImmediate(new LabelArgument(skip)));
		//Get rightx
		//Get right
		visit(host.right.get(), param);
		addOpcode(new OCastBoolF(new VarArgument(EXP1))); //Don't break boolean ops on negative numbers on the RHS.
		addOpcode(new OCompareImmediate(new VarArgument(EXP1), new LiteralArgument(1)));
		//Set output
		Opcode* ocode = (*lookupOption(*scope, CompileOption::OPT_BOOL_TRUE_RETURN_DECIMAL)) ? (Opcode*)(new OSetMore(new VarArgument(EXP1))) : (Opcode*)(new OSetMoreI(new VarArgument(EXP1)));
		if(short_circuit) ocode->setLabel(skip);
		addOpcode(ocode);
	}
	else
	{
		//Get left
		visit(host.left.get(), param);
		//Store left for later
		addOpcode(new OPushRegister(new VarArgument(EXP1)));
		//Get right
		visit(host.right.get(), param);
		//Retrieve left
		addOpcode(new OPopRegister(new VarArgument(EXP2)));
		addOpcode(new OCastBoolF(new VarArgument(EXP1)));
		addOpcode(new OCastBoolF(new VarArgument(EXP2)));
		addOpcode(new OAddRegister(new VarArgument(EXP1), new VarArgument(EXP2)));
		addOpcode(new OCompareImmediate(new VarArgument(EXP1), new LiteralArgument(1)));
		if(*lookupOption(*scope, CompileOption::OPT_BOOL_TRUE_RETURN_DECIMAL))
			addOpcode(new OSetMore(new VarArgument(EXP1)));
		else
			addOpcode(new OSetMoreI(new VarArgument(EXP1)));
	}
}

void BuildOpcodes::caseExprGT(ASTExprGT& host, void* param)
{
    if (host.getCompileTimeValue(NULL, scope))
    {
        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
        return;
    }

    //compute both sides
    visit(host.left.get(), param);
    addOpcode(new OPushRegister(new VarArgument(EXP1)));
    visit(host.right.get(), param);
    addOpcode(new OPopRegister(new VarArgument(EXP2)));
    addOpcode(new OCompareRegister(new VarArgument(EXP2), new VarArgument(EXP1)));
    addOpcode(new OSetLess(new VarArgument(EXP1)));
    addOpcode(new OCompareImmediate(new VarArgument(EXP1), new LiteralArgument(0)));
	if(*lookupOption(*scope, CompileOption::OPT_BOOL_TRUE_RETURN_DECIMAL))
		addOpcode(new OSetTrue(new VarArgument(EXP1)));
	else
		addOpcode(new OSetTrueI(new VarArgument(EXP1)));
}

void BuildOpcodes::caseExprGE(ASTExprGE& host, void* param)
{
    if (host.getCompileTimeValue(NULL, scope))
    {
        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
        return;
    }

    //compute both sides
    visit(host.left.get(), param);
    addOpcode(new OPushRegister(new VarArgument(EXP1)));
    visit(host.right.get(), param);
    addOpcode(new OPopRegister(new VarArgument(EXP2)));
    addOpcode(new OCompareRegister(new VarArgument(EXP2), new VarArgument(EXP1)));
	if(*lookupOption(*scope, CompileOption::OPT_BOOL_TRUE_RETURN_DECIMAL))
		addOpcode(new OSetMore(new VarArgument(EXP1)));
	else
		addOpcode(new OSetMoreI(new VarArgument(EXP1)));
}

void BuildOpcodes::caseExprLT(ASTExprLT& host, void* param)
{
    if (host.getCompileTimeValue(NULL, scope))
    {
        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
        return;
    }

    //compute both sides
    visit(host.left.get(), param);
    addOpcode(new OPushRegister(new VarArgument(EXP1)));
    visit(host.right.get(), param);
    addOpcode(new OPopRegister(new VarArgument(EXP2)));
    addOpcode(new OCompareRegister(new VarArgument(EXP2), new VarArgument(EXP1)));
    addOpcode(new OSetMore(new VarArgument(EXP1)));
    addOpcode(new OCompareImmediate(new VarArgument(EXP1), new LiteralArgument(0)));
	if(*lookupOption(*scope, CompileOption::OPT_BOOL_TRUE_RETURN_DECIMAL))
		addOpcode(new OSetTrue(new VarArgument(EXP1)));
	else
		addOpcode(new OSetTrueI(new VarArgument(EXP1)));
}

void BuildOpcodes::caseExprLE(ASTExprLE& host, void* param)
{
    if (host.getCompileTimeValue(NULL, scope))
    {
        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
        return;
    }

    // Compute both sides.
    visit(host.left.get(), param);
    addOpcode(new OPushRegister(new VarArgument(EXP1)));
    visit(host.right.get(), param);
    addOpcode(new OPopRegister(new VarArgument(EXP2)));
    addOpcode(new OCompareRegister(new VarArgument(EXP2), new VarArgument(EXP1)));
	if(*lookupOption(*scope, CompileOption::OPT_BOOL_TRUE_RETURN_DECIMAL))
		addOpcode(new OSetLess(new VarArgument(EXP1)));
	else
		addOpcode(new OSetLessI(new VarArgument(EXP1)));
}

void BuildOpcodes::caseExprEQ(ASTExprEQ& host, void* param)
{
    // Special case for booleans.
	DataType const* ltype = host.left->getReadType(scope, this);
	DataType const* rtype = host.right->getReadType(scope, this);
    bool isBoolean = (*ltype == DataType::BOOL || *rtype == DataType::BOOL || *ltype == DataType::CBOOL || *rtype == DataType::CBOOL);

    if (host.getCompileTimeValue(NULL, scope))
    {
        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
        return;
    }
	else
	{
		if(ASTExpr* lhs = host.left.get())
		{
			if(optional<int32_t> val = lhs->getCompileTimeValue(NULL, scope))
			{
				if((*val)==0)
				{
					visit(host.right.get(), param);
					addOpcode(new OCompareImmediate(new VarArgument(EXP1), new LiteralArgument(0)));
					if(*lookupOption(*scope, CompileOption::OPT_BOOL_TRUE_RETURN_DECIMAL))
						addOpcode(new OSetTrue(new VarArgument(EXP1)));
					else
						addOpcode(new OSetTrueI(new VarArgument(EXP1)));
					return;
				}
			}
		}
		if(ASTExpr* rhs = host.right.get())
		{
			if(optional<int32_t> val = rhs->getCompileTimeValue(NULL, scope))
			{
				if((*val)==0)
				{
					visit(host.left.get(), param);
					addOpcode(new OCompareImmediate(new VarArgument(EXP1), new LiteralArgument(0)));
					if(*lookupOption(*scope, CompileOption::OPT_BOOL_TRUE_RETURN_DECIMAL))
						addOpcode(new OSetTrue(new VarArgument(EXP1)));
					else
						addOpcode(new OSetTrueI(new VarArgument(EXP1)));
					return;
				}
			}
		}
	}

    // Compute both sides.
    visit(host.left.get(), param);
    addOpcode(new OPushRegister(new VarArgument(EXP1)));
    visit(host.right.get(), param);
    addOpcode(new OPopRegister(new VarArgument(EXP2)));

    if (isBoolean)
    {
        addOpcode(new OCastBoolF(new VarArgument(EXP1)));
        addOpcode(new OCastBoolF(new VarArgument(EXP2)));
    }

    addOpcode(new OCompareRegister(new VarArgument(EXP1), new VarArgument(EXP2)));
	if(*lookupOption(*scope, CompileOption::OPT_BOOL_TRUE_RETURN_DECIMAL))
		addOpcode(new OSetTrue(new VarArgument(EXP1)));
	else
		addOpcode(new OSetTrueI(new VarArgument(EXP1)));
}

void BuildOpcodes::caseExprNE(ASTExprNE& host, void* param)
{
    // Special case for booleans.
	DataType const* ltype = host.left->getReadType(scope, this);
	DataType const* rtype = host.right->getReadType(scope, this);
    bool isBoolean = (*ltype == DataType::BOOL || *rtype == DataType::BOOL || *ltype == DataType::CBOOL || *rtype == DataType::CBOOL);

    if (host.getCompileTimeValue(NULL, scope))
    {
        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
        return;
    }
	else
	{
		if(ASTExpr* lhs = host.left.get())
		{
			if(optional<int32_t> val = lhs->getCompileTimeValue(NULL, scope))
			{
				if((*val)==0)
				{
					visit(host.right.get(), param);
					if(*lookupOption(*scope, CompileOption::OPT_BOOL_TRUE_RETURN_DECIMAL))
						addOpcode(new OCastBoolF(new VarArgument(EXP1)));
					else
						addOpcode(new OCastBoolI(new VarArgument(EXP1)));
					return;
				}
			}
		}
		if(ASTExpr* rhs = host.right.get())
		{
			if(optional<int32_t> val = rhs->getCompileTimeValue(NULL, scope))
			{
				if((*val)==0)
				{
					visit(host.left.get(), param);
					if(*lookupOption(*scope, CompileOption::OPT_BOOL_TRUE_RETURN_DECIMAL))
						addOpcode(new OCastBoolF(new VarArgument(EXP1)));
					else
						addOpcode(new OCastBoolI(new VarArgument(EXP1)));
					return;
				}
			}
		}
	}

    // Compute both sides.
    visit(host.left.get(), param);
    addOpcode(new OPushRegister(new VarArgument(EXP1)));
    visit(host.right.get(), param);
    addOpcode(new OPopRegister(new VarArgument(EXP2)));

    if (isBoolean)
    {
        addOpcode(new OCastBoolF(new VarArgument(EXP1)));
        addOpcode(new OCastBoolF(new VarArgument(EXP2)));
    }

    addOpcode(new OCompareRegister(new VarArgument(EXP1), new VarArgument(EXP2)));
	if(*lookupOption(*scope, CompileOption::OPT_BOOL_TRUE_RETURN_DECIMAL))
		addOpcode(new OSetFalse(new VarArgument(EXP1)));
	else
		addOpcode(new OSetFalseI(new VarArgument(EXP1)));
}

void BuildOpcodes::caseExprAppxEQ(ASTExprAppxEQ& host, void* param)
{
    if (host.getCompileTimeValue(NULL, scope))
    {
        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
        return;
    }

    // Compute both sides.
    visit(host.left.get(), param);
    addOpcode(new OPushRegister(new VarArgument(EXP1)));
    visit(host.right.get(), param);
    addOpcode(new OPopRegister(new VarArgument(EXP2)));
	addOpcode(new OSubRegister(new VarArgument(EXP1), new VarArgument(EXP2)));
	addOpcode(new OAbsRegister(new VarArgument(EXP1)));
	
    addOpcode(new OCompareImmediate(new VarArgument(EXP1), new LiteralArgument(*lookupOption(*scope, CompileOption::OPT_APPROX_EQUAL_MARGIN))));
	if(*lookupOption(*scope, CompileOption::OPT_BOOL_TRUE_RETURN_DECIMAL))
		addOpcode(new OSetLess(new VarArgument(EXP1)));
	else
		addOpcode(new OSetLessI(new VarArgument(EXP1)));
}

void BuildOpcodes::caseExprXOR(ASTExprXOR& host, void* param)
{
	if (host.getCompileTimeValue(NULL, scope))
	{
		addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
		return;
	}
	
	// Compute both sides.
	visit(host.left.get(), param);
	addOpcode(new OPushRegister(new VarArgument(EXP1)));
	visit(host.right.get(), param);
	addOpcode(new OPopRegister(new VarArgument(EXP2)));
	
	addOpcode(new OCastBoolF(new VarArgument(EXP1)));
	addOpcode(new OCastBoolF(new VarArgument(EXP2)));
	
	addOpcode(new OCompareRegister(new VarArgument(EXP1), new VarArgument(EXP2)));
	if(*lookupOption(*scope, CompileOption::OPT_BOOL_TRUE_RETURN_DECIMAL))
		addOpcode(new OSetFalse(new VarArgument(EXP1)));
	else
		addOpcode(new OSetFalseI(new VarArgument(EXP1)));
}

void BuildOpcodes::caseExprPlus(ASTExprPlus& host, void* param)
{
    if (host.getCompileTimeValue(NULL, scope))
    {
        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
        return;
    }
	else
	{
		if(ASTExpr* lhs = host.left.get())
		{
			if(optional<int32_t> val = lhs->getCompileTimeValue(NULL, scope))
			{
				if((*val)==0) // 0 + y? Just do y!
				{
					visit(host.right.get(), param);
					return;
				}
			}
		}
		if(ASTExpr* rhs = host.right.get())
		{
			if(optional<int32_t> val = rhs->getCompileTimeValue(NULL, scope))
			{
				if((*val)==0) // x + 0? Just do x!
				{
					visit(host.left.get(), param);
					return;
				}
			}
		}
	}

    // Compute both sides.
    visit(host.left.get(), param);
    addOpcode(new OPushRegister(new VarArgument(EXP1)));
    visit(host.right.get(), param);
    addOpcode(new OPopRegister(new VarArgument(EXP2)));
    addOpcode(new OAddRegister(new VarArgument(EXP1), new VarArgument(EXP2)));
}

void BuildOpcodes::caseExprMinus(ASTExprMinus& host, void* param)
{
    if (host.getCompileTimeValue(NULL, scope))
    {
        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
        return;
    }
	else
	{
		if(ASTExpr* rhs = host.right.get())
		{
			if(optional<int32_t> val = rhs->getCompileTimeValue(NULL, scope))
			{
				if((*val)==0) // x - 0? Just do x!
				{
					visit(host.left.get(), param);
					return;
				}
			}
		}
	}

    // Compute both sides.
    visit(host.left.get(), param);
    addOpcode(new OPushRegister(new VarArgument(EXP1)));
    visit(host.right.get(), param);
    addOpcode(new OPopRegister(new VarArgument(EXP2)));
    addOpcode(new OSubRegister(new VarArgument(EXP2), new VarArgument(EXP1)));
    addOpcode(new OSetRegister(new VarArgument(EXP1), new VarArgument(EXP2)));
}

void BuildOpcodes::caseExprTimes(ASTExprTimes& host, void *param)
{
    if (host.getCompileTimeValue(NULL, scope))
    {
        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
        return;
    }
	else
	{
		if(ASTExpr* lhs = host.left.get())
		{
			if(optional<int32_t> val = lhs->getCompileTimeValue(NULL, scope))
			{
				if((*val)==10000L)
				{
					visit(host.right.get(), param);
					return;
				}
			}
		}
		if(ASTExpr* rhs = host.right.get())
		{
			if(optional<int32_t> val = rhs->getCompileTimeValue(NULL, scope))
			{
				if((*val)==10000L)
				{
					visit(host.left.get(), param);
					return;
				}
			}
		}
	}

    // Compute both sides.
    visit(host.left.get(), param);
    addOpcode(new OPushRegister(new VarArgument(EXP1)));
    visit(host.right.get(), param);
    addOpcode(new OPopRegister(new VarArgument(EXP2)));
    addOpcode(new OMultRegister(new VarArgument(EXP1), new VarArgument(EXP2)));
}

void BuildOpcodes::caseExprDivide(ASTExprDivide& host, void* param)
{
    if (host.getCompileTimeValue(NULL, scope))
    {
        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
        return;
    }

    // Compute both sides.
    visit(host.left.get(), param);
    addOpcode(new OPushRegister(new VarArgument(EXP1)));
    visit(host.right.get(), param);
    addOpcode(new OPopRegister(new VarArgument(EXP2)));
    addOpcode(new ODivRegister(new VarArgument(EXP2), new VarArgument(EXP1)));
    addOpcode(new OSetRegister(new VarArgument(EXP1), new VarArgument(EXP2)));
}

void BuildOpcodes::caseExprModulo(ASTExprModulo& host, void* param)
{
    if (host.getCompileTimeValue(NULL, scope))
    {
        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
        return;
    }

    // Compute both sides.
    visit(host.left.get(), param);
    addOpcode(new OPushRegister(new VarArgument(EXP1)));
    visit(host.right.get(), param);
    addOpcode(new OPopRegister(new VarArgument(EXP2)));
    addOpcode(new OModuloRegister(new VarArgument(EXP2), new VarArgument(EXP1)));
    addOpcode(new OSetRegister(new VarArgument(EXP1), new VarArgument(EXP2)));
}

void BuildOpcodes::caseExprBitAnd(ASTExprBitAnd& host, void* param)
{
    if (host.getCompileTimeValue(NULL, scope))
    {
        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
        return;
    }

    // Compute both sides.
    visit(host.left.get(), param);
    addOpcode(new OPushRegister(new VarArgument(EXP1)));
    visit(host.right.get(), param);
    addOpcode(new OPopRegister(new VarArgument(EXP2)));
	if(*lookupOption(*scope, CompileOption::OPT_BINARY_32BIT)
	   || host.left.get()->isLong(scope, this)
	   || host.right.get()->isLong(scope, this))
		addOpcode(new O32BitAndRegister(new VarArgument(EXP1), new VarArgument(EXP2)));
	else
		addOpcode(new OAndRegister(new VarArgument(EXP1), new VarArgument(EXP2)));
}

void BuildOpcodes::caseExprBitOr(ASTExprBitOr& host, void* param)
{
    if (host.getCompileTimeValue(NULL, scope))
    {
        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
        return;
    }

    // Compute both sides.
    visit(host.left.get(), param);
    addOpcode(new OPushRegister(new VarArgument(EXP1)));
    visit(host.right.get(), param);
    addOpcode(new OPopRegister(new VarArgument(EXP2)));
	if(*lookupOption(*scope, CompileOption::OPT_BINARY_32BIT)
	   || host.left.get()->isLong(scope, this)
	   || host.right.get()->isLong(scope, this))
		addOpcode(new O32BitOrRegister(new VarArgument(EXP1), new VarArgument(EXP2)));
	else
		addOpcode(new OOrRegister(new VarArgument(EXP1), new VarArgument(EXP2)));
}

void BuildOpcodes::caseExprBitXor(ASTExprBitXor& host, void* param)
{
    if (host.getCompileTimeValue(NULL, scope))
    {
        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
        return;
    }

    // Compute both sides.
    visit(host.left.get(), param);
    addOpcode(new OPushRegister(new VarArgument(EXP1)));
    visit(host.right.get(), param);
    addOpcode(new OPopRegister(new VarArgument(EXP2)));
	if(*lookupOption(*scope, CompileOption::OPT_BINARY_32BIT)
	   || host.left.get()->isLong(scope, this)
	   || host.right.get()->isLong(scope, this))
		addOpcode(new O32BitXorRegister(new VarArgument(EXP1), new VarArgument(EXP2)));
	else
		addOpcode(new OXorRegister(new VarArgument(EXP1), new VarArgument(EXP2)));
}

void BuildOpcodes::caseExprLShift(ASTExprLShift& host, void* param)
{
    if (host.getCompileTimeValue(NULL, scope))
    {
        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
        return;
    }

    // Compute both sides.
    visit(host.left.get(), param);
    addOpcode(new OPushRegister(new VarArgument(EXP1)));
    visit(host.right.get(), param);
    addOpcode(new OPopRegister(new VarArgument(EXP2)));
	if(*lookupOption(*scope, CompileOption::OPT_BINARY_32BIT)
	   || host.left.get()->isLong(scope, this))
		addOpcode(new O32BitLShiftRegister(new VarArgument(EXP2), new VarArgument(EXP1)));
	else
		addOpcode(new OLShiftRegister(new VarArgument(EXP2), new VarArgument(EXP1)));
    addOpcode(new OSetRegister(new VarArgument(EXP1), new VarArgument(EXP2)));
}

void BuildOpcodes::caseExprRShift(ASTExprRShift& host, void* param)
{
    if (host.getCompileTimeValue(NULL, scope))
    {
        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
        return;
    }

    // Compute both sides.
    visit(host.left.get(), param);
    addOpcode(new OPushRegister(new VarArgument(EXP1)));
    visit(host.right.get(), param);
    addOpcode(new OPopRegister(new VarArgument(EXP2)));
	if(*lookupOption(*scope, CompileOption::OPT_BINARY_32BIT)
	   || host.left.get()->isLong(scope, this))
		addOpcode(new O32BitRShiftRegister(new VarArgument(EXP2), new VarArgument(EXP1)));
	else
		addOpcode(new ORShiftRegister(new VarArgument(EXP2), new VarArgument(EXP1)));
    addOpcode(new OSetRegister(new VarArgument(EXP1), new VarArgument(EXP2)));
}

void BuildOpcodes::caseExprTernary(ASTTernaryExpr& host, void* param)
{
	if (host.getCompileTimeValue(NULL, scope))
	{
		addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
		return;
	}

	// Works just like an if/else, except for the "getCompileTimeValue(NULL, scope)" above!
	visit(host.left.get(), param);
	int32_t elseif = ScriptParser::getUniqueLabelID();
	int32_t endif = ScriptParser::getUniqueLabelID();
	addOpcode(new OCompareImmediate(new VarArgument(EXP1), new LiteralArgument(0)));
	addOpcode(new OGotoTrueImmediate(new LabelArgument(elseif)));
	visit(host.middle.get(), param); //Use middle section
	addOpcode(new OGotoImmediate(new LabelArgument(endif))); //Skip right
	Opcode *next = new OSetImmediate(new VarArgument(EXP2), new LiteralArgument(0));
	next->setLabel(elseif);
	addOpcode(next); //Add label for between middle and right
	visit(host.right.get(), param); //Use right section
	next = new OSetImmediate(new VarArgument(EXP2), new LiteralArgument(0));
	next->setLabel(endif);
	addOpcode(next); //Add label for after right
}

// Literals

void BuildOpcodes::caseNumberLiteral(ASTNumberLiteral& host, void*)
{
    if (host.getCompileTimeValue(NULL, scope))
        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
    else
    {
        pair<int32_t, bool> val = ScriptParser::parseLong(host.value->parseValue(this, scope), scope);

        if (!val.second)
	        handleError(CompileError::ConstTrunc(
			                    &host, host.value->value));

        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(val.first)));
    }
}

void BuildOpcodes::caseCharLiteral(ASTCharLiteral& host, void*)
{
    if (host.getCompileTimeValue(NULL, scope))
        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
    else
    {
        pair<int32_t, bool> val = ScriptParser::parseLong(host.value->parseValue(this, scope), scope);

        if (!val.second)
	        handleError(CompileError::ConstTrunc(
			                    &host, host.value->value));

        addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(val.first)));
    }
}

void BuildOpcodes::caseBoolLiteral(ASTBoolLiteral& host, void*)
{
    addOpcode(new OSetImmediate(new VarArgument(EXP1), new LiteralArgument(*host.getCompileTimeValue(this, scope))));
}

void BuildOpcodes::caseStringLiteral(ASTStringLiteral& host, void* param)
{
	OpcodeContext& context = *(OpcodeContext*)param;
	if (host.declaration) stringLiteralDeclaration(host, context);
	else stringLiteralFree(host, context);
}

void BuildOpcodes::stringLiteralDeclaration(
		ASTStringLiteral& host, OpcodeContext& context)
{
	ASTDataDecl& declaration = *host.declaration;
	Datum& manager = *declaration.manager;
	string const& data = host.value;

	// Grab the size from the declaration.
	int32_t size = -1;
	if (declaration.extraArrays.size() == 1)
	{
		ASTDataDeclExtraArray& extraArray = *declaration.extraArrays[0];
		if (optional<int32_t> totalSize = extraArray.getCompileTimeSize(this, scope))
			size = *totalSize;
		else if (extraArray.hasSize())
		{
			handleError(CompileError::ExprNotConstant(&host));
			return;
		}
	}

	// Otherwise, grab the number of elements as the size.
	if (size == -1) size = data.size() + 1;

	// Make sure the chosen size has enough space.
	if (size < int32_t(data.size() + 1))
	{
		handleError(CompileError::ArrayListStringTooLarge(&host));
		return;
	}

	// Create the array and store its id.
	if (optional<int32_t> globalId = manager.getGlobalId())
	{
		addOpcode(new OAllocateGlobalMemImmediate(
				          new VarArgument(EXP1),
				          new LiteralArgument(size * 10000L)));
		addOpcode(new OSetRegister(new GlobalArgument(*globalId),
		                           new VarArgument(EXP1)));
	}
	else
	{
		addOpcode(new OAllocateMemImmediate(new VarArgument(EXP1),
		                                    new LiteralArgument(size * 10000L)));
		int32_t offset = 10000L * *getStackOffset(manager);
		addOpcode(new OSetRegister(new VarArgument(SFTEMP), new VarArgument(SFRAME)));
		addOpcode(new OAddImmediate(new VarArgument(SFTEMP), new LiteralArgument(offset)));
		addOpcode(new OStoreIndirect(new VarArgument(EXP1), new VarArgument(SFTEMP)));
		// Register for cleanup.
		arrayRefs.push_back(offset);
	}

	// Initialize array.
	addOpcode(new OSetRegister(new VarArgument(INDEX),
	                           new VarArgument(EXP1)));
	for (int32_t i = 0; i < (int32_t)data.size(); ++i)
	{
		addOpcode(new OWritePODArrayII(
				          new LiteralArgument(i * 10000L),
						  new LiteralArgument(data[i] * 10000L)));
	}
	//Add nullchar
	addOpcode(new OWritePODArrayII(
					  new LiteralArgument(data.size() * 10000L),
					  new LiteralArgument(0)));
}

void BuildOpcodes::stringLiteralFree(
		ASTStringLiteral& host, OpcodeContext& context)
{
	Literal& manager = *host.manager;
	string data = host.value;
	int32_t size = data.size() + 1;
	int32_t offset = *getStackOffset(manager) * 10000L;
	vector<shared_ptr<Opcode>>& init = context.initCode;

	////////////////////////////////////////////////////////////////
	// Initialization Code.

	// Allocate.
	addOpcode2(init, new OAllocateMemImmediate(
			               new VarArgument(EXP1),
			               new LiteralArgument(size * 10000L)));
	addOpcode2(init, new OSetRegister(new VarArgument(SFTEMP),
	                                new VarArgument(SFRAME)));
	addOpcode2(init, new OAddImmediate(new VarArgument(SFTEMP),
	                                 new LiteralArgument(offset)));
	addOpcode2(init, new OStoreIndirect(new VarArgument(EXP1),
	                                  new VarArgument(SFTEMP)));

	// Initialize.
	addOpcode2(init, new OSetRegister(new VarArgument(INDEX),
	                                new VarArgument(EXP1)));
	for (int32_t i = 0; i < (int32_t)data.size(); ++i)
	{
		addOpcode2(init, new OWritePODArrayII(
				               new LiteralArgument(i * 10000L),
				               new LiteralArgument(data[i] * 10000L)));
	}
	//Add nullchar
	addOpcode2(init, new OWritePODArrayII(
			               new LiteralArgument(data.size() * 10000L),
			               new LiteralArgument(0)));

	////////////////////////////////////////////////////////////////
	// Actual Code.

	// Local variable, get its value from the stack.
	addOpcode(new OSetRegister(new VarArgument(SFTEMP),
	                           new VarArgument(SFRAME)));
	addOpcode(new OAddImmediate(new VarArgument(SFTEMP),
	                            new LiteralArgument(offset)));
	addOpcode(new OLoadIndirect(new VarArgument(EXP1),
	                            new VarArgument(SFTEMP)));

	////////////////////////////////////////////////////////////////
	// Register for cleanup.

	arrayRefs.push_back(offset);
}

void BuildOpcodes::caseArrayLiteral(ASTArrayLiteral& host, void* param)
{
	OpcodeContext& context = *(OpcodeContext*)param;
	if (host.declaration) arrayLiteralDeclaration(host, context);
	else arrayLiteralFree(host, context);
}

void BuildOpcodes::arrayLiteralDeclaration(
		ASTArrayLiteral& host, OpcodeContext& context)
{
	ASTDataDecl& declaration = *host.declaration;
	Datum& manager = *declaration.manager;

	// Find the size.
	int32_t size = -1;
	// From this literal?
	if (host.size)
		if (optional<int32_t> s = host.size->getCompileTimeValue(this, scope))
			size = *s / 10000L;
	// From the declaration?
	if (size == -1 && declaration.extraArrays.size() == 1)
	{
		ASTDataDeclExtraArray& extraArray = *declaration.extraArrays[0];
		if (optional<int32_t> totalSize = extraArray.getCompileTimeSize(this, scope))
			size = *totalSize;
		else if (extraArray.hasSize())
		{
			handleError(CompileError::ExprNotConstant(&host));
			return;
		}
	}
	// Otherwise, grab the number of elements as the size.
	if (size == -1) size = host.elements.size();

	// Make sure we have a valid size.
	if (size < 1)
	{
		handleError(CompileError::ArrayTooSmall(&host));
		return;
	}
	
	// Make sure the chosen size has enough space.
	if (size < int32_t(host.elements.size()))
	{
		handleError(CompileError::ArrayListTooLarge(&host));
		return;
	}

	// Create the array and store its id.
	if (optional<int32_t> globalId = manager.getGlobalId())
	{
		addOpcode(new OAllocateGlobalMemImmediate(
				          new VarArgument(EXP1),
				          new LiteralArgument(size * 10000L)));
		addOpcode(new OSetRegister(new GlobalArgument(*globalId),
		                           new VarArgument(EXP1)));
	}
	else
	{
		addOpcode(new OAllocateMemImmediate(new VarArgument(EXP1),
		                                    new LiteralArgument(size * 10000L)));
		int32_t offset = 10000L * *getStackOffset(manager);
		addOpcode(new OSetRegister(new VarArgument(SFTEMP), new VarArgument(SFRAME)));
		addOpcode(new OAddImmediate(new VarArgument(SFTEMP), new LiteralArgument(offset)));
		addOpcode(new OStoreIndirect(new VarArgument(EXP1), new VarArgument(SFTEMP)));
		// Register for cleanup.
		arrayRefs.push_back(offset);
	}

	// Initialize array.
	addOpcode(new OSetRegister(new VarArgument(INDEX),
	                           new VarArgument(EXP1)));
	int32_t i = 0;
	for (vector<ASTExpr*>::const_iterator it = host.elements.begin();
		 it != host.elements.end(); ++it, i += 10000L)
	{
		if (optional<int32_t> val = (*it)->getCompileTimeValue(this, scope))
		{
			addOpcode(new OWritePODArrayII(new LiteralArgument(i),
			                               new LiteralArgument(*val)));
		}
		else
		{
			addOpcode(new OPushRegister(new VarArgument(INDEX)));
			visit(*it, &context);
			addOpcode(new OPopRegister(new VarArgument(INDEX)));
			addOpcode(new OWritePODArrayIR(new LiteralArgument(i),
			                               new VarArgument(EXP1)));
		}
	}
	
	////////////////////////////////////////////////////////////////
	// Actual Code.
/* I added this because calling an 'internal function with an array literal, inside a user
	created function is using SETV with bizarre values. -ZScript
	//Didn't work.
	int32_t offset = *getStackOffset(manager) * 10000L;
	// Local variable, get its value from the stack.
	addOpcode(new OSetRegister(new VarArgument(SFTEMP),
	                           new VarArgument(SFRAME)));
	addOpcode(new OAddImmediate(new VarArgument(SFTEMP),
	                            new LiteralArgument(offset)));
	addOpcode(new OLoadIndirect(new VarArgument(EXP1),
	                            new VarArgument(SFTEMP)));
	*/
}

void BuildOpcodes::arrayLiteralFree(
		ASTArrayLiteral& host, OpcodeContext& context)
{
	Literal& manager = *host.manager;

	int32_t size = -1;

	// If there's an explicit size, grab it.
	if (host.size)
	{
		if (optional<int32_t> s = host.size->getCompileTimeValue(this, scope))
			size = *s / 10000L;
		else
		{
			handleError(CompileError::ExprNotConstant(host.size.get()));
			return;
		}
	}

	// Otherwise, grab the number of elements.
	if (size == -1) size = host.elements.size();

	// Make sure the chosen size has enough space.
	if (size < int32_t(host.elements.size()))
	{
		handleError(CompileError::ArrayListTooLarge(&host));
		return;
	}

	int32_t offset = 10000L * *getStackOffset(manager);
	
	////////////////////////////////////////////////////////////////
	// Initialization Code.

	// Allocate.

	addOpcode2(context.initCode,
			new OAllocateMemImmediate(new VarArgument(EXP1),
			                          new LiteralArgument(size * 10000L)));
	addOpcode2(context.initCode,
			new OSetRegister(new VarArgument(SFTEMP),
			                 new VarArgument(SFRAME)));
	addOpcode2(context.initCode,
			new OAddImmediate(new VarArgument(SFTEMP),
			                  new LiteralArgument(offset)));
	addOpcode2(context.initCode,
			new OStoreIndirect(new VarArgument(EXP1),
			                   new VarArgument(SFTEMP)));

	// Initialize.
	addOpcode2(context.initCode, new OSetRegister(new VarArgument(INDEX),
	                                            new VarArgument(EXP1)));
	int32_t i = 0;
	for (vector<ASTExpr*>::iterator it = host.elements.begin();
		 it != host.elements.end(); ++it, i += 10000L)
	{
		if (optional<int32_t> val = (*it)->getCompileTimeValue(this, scope))
		{
			addOpcode2(context.initCode, new OWritePODArrayII(new LiteralArgument(i),
			                                                new LiteralArgument(*val)));
		}
		else
		{
			addOpcode2(context.initCode, new OPushRegister(new VarArgument(INDEX)));
			opcodeTargets.push_back(&context.initCode);
			visit(*it, &context);
			opcodeTargets.pop_back();
			addOpcode2(context.initCode, new OPopRegister(new VarArgument(INDEX)));
			addOpcode2(context.initCode, new OWritePODArrayIR(new LiteralArgument(i),
			                                                new VarArgument(EXP1)));
		}
	}

	////////////////////////////////////////////////////////////////
	// Actual Code.

	// Local variable, get its value from the stack.
	addOpcode(new OSetRegister(new VarArgument(SFTEMP),
	                           new VarArgument(SFRAME)));
	addOpcode(new OAddImmediate(new VarArgument(SFTEMP),
	                            new LiteralArgument(offset)));
	addOpcode(new OLoadIndirect(new VarArgument(EXP1),
	                            new VarArgument(SFTEMP)));

	////////////////////////////////////////////////////////////////
	// Register for cleanup.

	arrayRefs.push_back(offset);
}

void BuildOpcodes::caseOptionValue(ASTOptionValue& host, void*)
{
	addOpcode(new OSetImmediate(new VarArgument(EXP1),
	                            new LiteralArgument(*host.getCompileTimeValue(this, scope))));
}

void BuildOpcodes::caseIsIncluded(ASTIsIncluded& host, void*)
{
	addOpcode(new OSetImmediate(new VarArgument(EXP1),
	                            new LiteralArgument(*host.getCompileTimeValue(this, scope))));
}

/////////////////////////////////////////////////////////////////////////////////
// LValBOHelper

LValBOHelper::LValBOHelper(Scope* scope)
{
	ASTVisitor::scope = scope;
}

void LValBOHelper::caseDefault(void *)
{
    //Shouldn't happen
    assert(false);
}

void LValBOHelper::addOpcode(Opcode* code)
{
	addOpcode2(result, code);
}

void LValBOHelper::addOpcode(std::shared_ptr<Opcode> &code)
{
	result.push_back(code);
}

template <class Container>
void LValBOHelper::addOpcodes(Container const& container)
{
	for (auto ptr: container)
		addOpcode(ptr);
}

/*
void LValBOHelper::caseDataDecl(ASTDataDecl& host, void* param)
{
    // Cannot be a global variable, so just stuff it in the stack
    OpcodeContext* c = (OpcodeContext*)param;
    int32_t vid = host.manager->id;
    int32_t offset = c->stackframe->getOffset(vid);
    addOpcode(new OSetRegister(new VarArgument(SFTEMP), new VarArgument(SFRAME)));
    addOpcode(new OAddImmediate(new VarArgument(SFTEMP), new LiteralArgument(offset)));
    addOpcode(new OStoreIndirect(new VarArgument(EXP1), new VarArgument(SFTEMP)));
}
*/

void LValBOHelper::caseExprIdentifier(ASTExprIdentifier& host, void* param)
{
    OpcodeContext* c = (OpcodeContext*)param;
    int32_t vid = host.binding->id;

    if (optional<int32_t> globalId = host.binding->getGlobalId())
    {
        // Global variable.
        addOpcode(new OSetRegister(new GlobalArgument(*globalId),
                                   new VarArgument(EXP1)));
        return;
    }

    // Set the stack.
    int32_t offset = 10000L * *getStackOffset(*host.binding);

    addOpcode(new OSetRegister(new VarArgument(SFTEMP),
                               new VarArgument(SFRAME)));
    addOpcode(new OAddImmediate(new VarArgument(SFTEMP),
                                new LiteralArgument(offset)));
    addOpcode(new OStoreIndirect(new VarArgument(EXP1),
                                 new VarArgument(SFTEMP)));
}

void LValBOHelper::caseExprArrow(ASTExprArrow &host, void *param)
{
    OpcodeContext *c = (OpcodeContext *)param;
    int32_t isIndexed = (host.index != NULL);
	assert(host.writeFunction->isInternal());
	
	if(host.writeFunction->getFlag(FUNCFLAG_INLINE))
	{
		if (!(host.writeFunction->internal_flags & IFUNCFLAG_SKIPPOINTER))
		{
			//Push rval
			addOpcode(new OPushRegister(new VarArgument(EXP1)));
			//Get lval
			BuildOpcodes oc(scope);
			oc.visit(host.left.get(), param);
			addOpcodes(oc.getResult());
			//Pop rval
			addOpcode(new OPopRegister(new VarArgument(EXP2)));
			//Push lval
			addOpcode(new OPushRegister(new VarArgument(EXP1)));
			//Push rval
			addOpcode(new OPushRegister(new VarArgument(EXP2)));
		}
		else
		{
			//Push rval
			addOpcode(new OPushRegister(new VarArgument(EXP1)));
		}
		
		if(isIndexed)
		{
			BuildOpcodes oc2(scope);
			oc2.visit(host.index.get(), param);
			addOpcodes(oc2.getResult());
			addOpcode(new OPushRegister(new VarArgument(EXP1)));
		}
		
		std::vector<std::shared_ptr<Opcode>> const& funcCode = host.writeFunction->getCode();
		for(auto it = funcCode.begin();
			it != funcCode.end(); ++it)
		{
			addOpcode((*it)->makeClone());
		}
	}
	else
	{
		// This is actually implemented as a settor function call.

		// Push the stack frame.
		addOpcode(new OPushRegister(new VarArgument(SFRAME)));

		int32_t returnlabel = ScriptParser::getUniqueLabelID();
		//push the return address
		addOpcode(new OPushImmediate(new LabelArgument(returnlabel)));
		
		if (!(host.writeFunction->internal_flags & IFUNCFLAG_SKIPPOINTER))
		{
			//Push rval
			addOpcode(new OPushRegister(new VarArgument(EXP1)));
			//Get lval
			BuildOpcodes oc(scope);
			oc.visit(host.left.get(), param);
			addOpcodes(oc.getResult());
			//Pop rval
			addOpcode(new OPopRegister(new VarArgument(EXP2)));
			//Push lval
			addOpcode(new OPushRegister(new VarArgument(EXP1)));
			//Push rval
			addOpcode(new OPushRegister(new VarArgument(EXP2)));
		}
		else
		{
			//Push rval
			addOpcode(new OPushRegister(new VarArgument(EXP1)));
		}
		
		//and push the index, if indexed
		if(isIndexed)
		{
			BuildOpcodes oc2(scope);
			oc2.visit(host.index.get(), param);
			addOpcodes(oc2.getResult());
			addOpcode(new OPushRegister(new VarArgument(EXP1)));
		}
		
		//finally, goto!
		int32_t label = host.writeFunction->getLabel();
		addOpcode(new OGotoImmediate(new LabelArgument(label)));

		// Pop the stack frame
		Opcode* next = new OPopRegister(new VarArgument(SFRAME));
		next->setLabel(returnlabel);
		addOpcode(next);
	}
}

void LValBOHelper::caseExprIndex(ASTExprIndex& host, void* param)
{
	// Arrows just fall back on the arrow implementation.
	if (host.array->isTypeArrow())
	{
		caseExprArrow(static_cast<ASTExprArrow&>(*host.array), param);
		return;
	}

	vector<shared_ptr<Opcode>> opcodes;
	BuildOpcodes bo(scope);
	optional<int32_t> arrVal = host.array->getCompileTimeValue(&bo, scope);
	optional<int32_t> indxVal = host.index->getCompileTimeValue(&bo, scope);
	if(!arrVal || !indxVal)
	{
		// Push the value.
		addOpcode(new OPushRegister(new VarArgument(EXP1)));
	}
	
	if(!arrVal)
	{
		// Get and push the array pointer.
		BuildOpcodes buildOpcodes1(scope);
		buildOpcodes1.visit(host.array.get(), param);
		opcodes = buildOpcodes1.getResult();
		for (auto it = opcodes.begin(); it != opcodes.end(); ++it)
			addOpcode(*it);
		if(!indxVal)
		{
			addOpcode(new OPushRegister(new VarArgument(EXP1)));
		}
		else addOpcode(new OSetRegister(new VarArgument(INDEX), new VarArgument(EXP1)));
	}
	if(!indxVal)
	{
		// Get the index.
		BuildOpcodes buildOpcodes2(scope);
		buildOpcodes2.visit(host.index.get(), param);
		opcodes = buildOpcodes2.getResult();
		for (auto it = opcodes.begin(); it != opcodes.end(); ++it)
			addOpcode(*it);
		addOpcode(new OSetRegister(new VarArgument(EXP2), new VarArgument(EXP1))); //can't be helped, unforunately -V
	}
	// Setup array indices.
	if(arrVal)
		addOpcode(new OSetImmediate(new VarArgument(INDEX), new LiteralArgument(*arrVal)));
    else if(!indxVal) addOpcode(new OPopRegister(new VarArgument(INDEX)));
	
	if(!arrVal || !indxVal)
	{
		addOpcode(new OPopRegister(new VarArgument(EXP1))); // Pop the value
	}
	if(indxVal) addOpcode(new OWritePODArrayIR(new LiteralArgument(*indxVal), new VarArgument(EXP1)));
	else addOpcode(new OWritePODArrayRR(new VarArgument(EXP2), new VarArgument(EXP1)));
}

