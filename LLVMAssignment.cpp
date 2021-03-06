//===- Hello.cpp - Example code from "Writing an LLVM Pass" ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements two versions of the LLVM "Hello World" pass described
// in docs/WritingAnLLVMPass.html
//
//===----------------------------------------------------------------------===//

#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Pass.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Utils.h>
#include <llvm/IR/Instructions.h>
#include <list>
#include <map>
#include <llvm/Transforms/Scalar/SimplifyCFG.h>

using namespace llvm;
static ManagedStatic<LLVMContext> GlobalContext;
static LLVMContext &getGlobalContext() { return *GlobalContext; }
/* In LLVM 5.0, when  -O0 passed to clang , the functions generated with clang
 * will have optnone attribute which would lead to some transform passes
 * disabled, like mem2reg.
 */
struct EnableFunctionOptPass: public FunctionPass {
    static char ID;
    EnableFunctionOptPass():FunctionPass(ID){}
    bool runOnFunction(Function & F) override{
        if(F.hasFnAttribute(Attribute::OptimizeNone))
        {
            F.removeFnAttr(Attribute::OptimizeNone);
        }
        return true;
    }
};
char EnableFunctionOptPass::ID = 0;

///!TODO TO BE COMPLETED BY YOU FOR ASSIGNMENT 2
/// Updated 11/10/2017 by fargo: make all functions
/// processed by mem2reg before this pass.
struct FuncPtrPass : public ModulePass {
    static char ID;  // Pass identification, replacement for typeid
    FuncPtrPass() : ModulePass(ID) {}

	std::map<int, std::list<Function*>> result;	// call指令对应到哪些函数

    bool runOnModule(Module &M) override {
		bool res = false;
		Module *tmp = &M;
		for (Module::iterator ff = tmp->begin(); ff != tmp->end(); ++ff)
			for (Function::iterator bb = ff->begin(); bb != ff->end(); ++bb)
				for (BasicBlock::iterator ii = bb->begin(); ii != bb->end(); ++ii)
					res |= checkCallInst(dyn_cast<CallInst>(ii));

		for (std::map<int, std::list<Function*>>::iterator it = result.begin(); it != result.end(); ++it){
			errs() << it->first << " : ";
			it->second.sort();
			it->second.unique();
			std::list<Function*>::iterator funcEnd = it->second.end();
			--funcEnd;
			for (std::list<Function*>::iterator func = it->second.begin(); func != funcEnd; ++func){
				errs() << (*func)->getName() << ',';
			}
			errs() << (*funcEnd)->getName() << '\n';
		}
        return false;
    }

	bool checkCallInst(CallInst *callInst) {
		if(!callInst)return false;
		int line = callInst->getDebugLoc().getLine();
		if(Function *func = callInst->getCalledFunction()){	// 最简单的直接函数调用
			result[line].push_back(func);
		}else if(Value *value = callInst->getCalledOperand()){
			result[line].splice(result[line].end(), std::list<Function*>(solveValue(value)));
		}else{
			errs() << "ERROR\n";
		}
		return false;
	}

	/*
	 *	caller: 调用语句
	 *	func: 调用的函数
	 *	values: 要解析的值
	 *	basicBlock: 函数定义所在的基本块
	 */
	std::list<Function*> solveFunction(CallInst *caller, Function *func, std::list<Value*> values, BasicBlock* basicBlock) {
		std::list<Function*> tmp;
		while(!values.empty()){
			Value *value = values.front();
			if (Function *theFunc = dyn_cast<Function>(value)) {
				tmp.push_back(theFunc);
			} else if (CallInst *callInst = dyn_cast<CallInst>(value)) {
				errs() << "UNIMPLEMNTED\n";
			} else if(PHINode *phi = dyn_cast<PHINode>(value)) {
				for(Value *phivalue : phi->incoming_values())
					values.push_back(phivalue);
			} else if(Argument *arg = dyn_cast<Argument>(value)) {
				unsigned argIdx = arg->getArgNo();
				BasicBlock *curBlock = caller->getParent();
				Value *valueToSolve = caller->getArgOperand(std::move(argIdx));
				valueToSolve = valueToSolve->DoPHITranslation(std::move(curBlock), basicBlock);
				std::list<Function*> solveValueRes = solveValue(std::move(valueToSolve));
				tmp.splice(tmp.end(), solveValueRes);
			} else {
			}
			values.pop_front();
		}
		return tmp;
	}

	std::list<Function*> solveReturnVal(CallInst *callInst) {
		std::list<Function*> tmp;
		if (Function *doubleCall = callInst->getCalledFunction()) {
			std::list<Value*> valuesToSolve = getReturnVal(doubleCall);
			tmp.splice(tmp.end(), solveFunction(callInst, doubleCall, std::move(valuesToSolve), callInst->getParent()));
		} else if (PHINode *phi = dyn_cast<PHINode>(callInst->getCalledOperand())){
			for (BasicBlock *block : phi->blocks()){
				std::list<Function*> solveTheVal = solveValue(phi->getIncomingValueForBlock(block));
				for (Function *func : solveTheVal){
					std::list<Value*> valuesToSolve = getReturnVal(func);
					tmp.splice(tmp.end(), solveFunction(callInst, func, std::move(valuesToSolve), block));
				}
			}
		} else {
			std::list<Function*> solveTheVal = solveValue(callInst->getCalledOperand());
			for (Function *func : solveTheVal){
				std::list<Value*> valuesToSolve = getReturnVal(func);
				tmp.splice(tmp.end(), solveFunction(callInst, func, std::move(valuesToSolve), callInst->getParent()));
			}
		}
		return tmp;
	}

	std::list<Value*> getReturnVal(Function *func) {
		std::list<Value*> tmp;
		for (Function::iterator bb = func->begin(); bb != func->end(); ++bb){
			if (ReturnInst *ret = dyn_cast<ReturnInst>(bb->getTerminator())){
				tmp.push_back(ret->getReturnValue());
			}
		}
		return tmp;
	}

	std::list<Function*> solveValue(Value *origin) {
		std::list<Function*> tmp;
		std::list<Value*> solveRes;
		solveRes.push_back(origin);
		while(!solveRes.empty()) {
			Value *value = solveRes.front();
			if (Function *theFunc = dyn_cast<Function>(value)) {	// the value is a function
				tmp.push_back(theFunc);
			} else if (CallInst *callInst = dyn_cast<CallInst>(value)) {	// return value of some function, maybe phinode
				tmp.splice(tmp.end(), solveReturnVal(callInst));
			} else if(PHINode *phi = dyn_cast<PHINode>(value)) {	// a phinode
				for(Value *phivalue : phi->incoming_values())
					solveRes.push_back(phivalue);
			} else if(Argument *arg = dyn_cast<Argument>(value)) {	// a argument of current function
				tmp.splice(tmp.end(), solveArgument(arg));
			} else if (ConstantPointerNull *nptr = dyn_cast<ConstantPointerNull>(value)) {
			} else if (SelectInst *selectInst = dyn_cast<SelectInst>(value)) {
				int mark = 2;
				if (CmpInst *cmpInst = dyn_cast<CmpInst>(selectInst->getCondition()))
					mark = solveCmpInst(cmpInst);
				if (mark == 0)
					solveRes.push_back(selectInst->getTrueValue());
				else if (mark == 1)
					solveRes.push_back(selectInst->getFalseValue());
				else {
					solveRes.push_back(selectInst->getTrueValue());
					solveRes.push_back(selectInst->getFalseValue());
				}
			} else {
				errs() << "ERROR here\n";
			}
			solveRes.pop_front();
		}
		return tmp;
	}

	// 0 代表只取TrueValue
	// 1 代表只取FalseValue
	// 2 代表两者都取
	int solveCmpInst(CmpInst *cmpInst) {
		Value *operand0 = cmpInst->getOperand(0);
		Value *operand1 = cmpInst->getOperand(1);
		int64_t item0, item1;
		if (ConstantInt *cons0 = dyn_cast<ConstantInt>(operand0)) {
			item0 = cons0->getSExtValue();
		} else {
			return 2;
		}
		if (ConstantInt *cons1 = dyn_cast<ConstantInt>(operand1)) {
			item1 = cons1->getSExtValue();
		} else {
			return 2;
		}
		int res = 1;
		switch(cmpInst->getPredicate()) {
			case CmpInst::ICMP_EQ:
				if (item0 == item1) res = 0;
				break;
			case CmpInst::ICMP_NE:
				if (item0 != item1) res = 0;
				break;
			case CmpInst::ICMP_UGT:
			case CmpInst::ICMP_SGT:
				if (item0 > item1) res = 0;
				break;
			case CmpInst::ICMP_UGE:
			case CmpInst::ICMP_SGE:
				if (item0 >= item1) res = 0;
				break;
			case CmpInst::ICMP_ULT:
			case CmpInst::ICMP_SLT:
				if (item0 < item1) res = 0;
				break;
			case CmpInst::ICMP_ULE:
			case CmpInst::ICMP_SLE:
				if (item0 <= item1) res = 0;
				break;
			default:
				errs() << "abaaba\n";
				break;
		};
		return res;
	}

	std::list<Function*> solveArgument(Argument *arg) {
		std::list<Function*> tmp;
		Function *parentFunction = arg->getParent();
		unsigned argIdx = arg->getArgNo();
		for(User *user : parentFunction->users()) {
			if (CallInst *callInst = dyn_cast<CallInst>(user)) {
				if (parentFunction == callInst->getCalledFunction()){
					Value *valueToSolve = callInst->getArgOperand(argIdx);
					tmp.splice(tmp.end(), solveValue(valueToSolve));
				} else {
					int argNumber = callInst->getNumArgOperands();
					for (int i = 0; i < argNumber; ++i) {
						if (parentFunction == dyn_cast<Function>(callInst->getArgOperand(i))) {
							auto calledFunctions = solveValue(callInst->getCalledOperand());
							for (Function *func : calledFunctions) {
								auto valuesToSolve = solveArgAsFunc(callInst, func, argIdx);
								tmp.splice(tmp.end(), solveFunction(callInst, func, valuesToSolve, callInst->getParent()));
							}
						}
					}
				}
			} else {
				errs() << "for argument user, not a call instruction!\n";
			}
		}
		return tmp;
	}

	std::list<Value*> solveArgAsFunc(CallInst *callInst, Function *func, int argIdx) {
		std::list<Value*> tmp;
		for (Function::iterator bb = func->begin(); bb != func->end(); ++bb) {
			for (BasicBlock::iterator ii = bb->begin(); ii != bb->end(); ++ii) {
				if (CallInst *it = dyn_cast<CallInst>(ii)){
					if (Argument* calledArg = dyn_cast<Argument>(it->getCalledOperand()))
						if (calledArg->getArgNo() == argIdx)
							tmp.push_back(it->getArgOperand(argIdx));
				}
			}
		}
		return tmp;
	}
};

char FuncPtrPass::ID = 0;
static RegisterPass<FuncPtrPass> X("funcptrpass",
                                   "Print function call instruction");

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("<filename>.bc"),
                                          cl::init(""));

int main(int argc, char **argv) {
    LLVMContext &Context = getGlobalContext();
    SMDiagnostic Err;
    // Parse the command line to read the Inputfilename
    cl::ParseCommandLineOptions(
        argc, argv,
        "FuncPtrPass \n My first LLVM too which does not do much.\n");

    // Load the input module
    std::unique_ptr<Module> M = parseIRFile(InputFilename, Err, Context);
    if (!M) {
        Err.print(argv[0], errs());
        return 1;
    }

    llvm::legacy::PassManager Passes;

    /// Remove functions' optnone attribute in LLVM5.0
    Passes.add(new EnableFunctionOptPass());
    /// Transform it to SSA
    Passes.add(llvm::createPromoteMemoryToRegisterPass());
	/// Simplify CFG
	Passes.add(llvm::createCFGSimplificationPass());

    /// Your pass to print Function and Call Instructions
    Passes.add(new FuncPtrPass());
    Passes.run(*M.get());
}
