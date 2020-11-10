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

	std::map<CallInst*, std::list<Function*>> result;	// call指令对应到哪些函数

    bool runOnModule(Module &M) override {
		bool res = false;
		Module *tmp = &M;
		for (Module::iterator ff = tmp->begin(); ff != tmp->end(); ++ff)
			for (Function::iterator bb = ff->begin(); bb != ff->end(); ++bb)
				for (BasicBlock::iterator ii = bb->begin(); ii != bb->end(); ++ii)
					res |= checkCallInst(dyn_cast<CallInst>(ii));
			
		for (std::map<CallInst*, std::list<Function*>>::iterator it = result.begin(); it != result.end(); ++it){
			errs() << it->first->getDebugLoc().getLine() << " : ";
			it->second.sort();
			it->second.unique();
			for (std::list<Function*>::iterator func = it->second.begin(); func != it->second.end(); ++func){
				errs() << (*func)->getName() << ' ';
			}
			errs() << '\n';
		}
        return false;
    }

	bool checkCallInst(CallInst *callInst) {
		if(!callInst)return false;
		errs() << callInst->getDebugLoc().getLine() << '\n';
		if(Function *func = callInst->getCalledFunction()){	// 最简单的直接函数调用
			result[callInst].push_back(func);
		}else if(Value *value = callInst->getCalledOperand()){
			result[callInst] = std::list<Function*>(solveValue(value));
		}else{
			errs() << "ERROR\n";
		}
		return false;
	}

	std::list<Function*> solveFunction(CallInst *caller, Function *func) {
		std::list<Function*> tmp;
		std::list<Value*> solveRes;
		for (Function::iterator bb = func->begin(); bb != func->end(); ++bb)
			if (ReturnInst *ret = dyn_cast<ReturnInst>(bb->getTerminator()))
				solveRes.push_back(ret->getReturnValue());
		while(!solveRes.empty()){
			Value *value = solveRes.front();
			if (Function *theFunc = dyn_cast<Function>(value)) {
				tmp.push_back(theFunc);
			} else if (CallInst *callInst = dyn_cast<CallInst>(value)) {
				if (Function *doubleCall = callInst->getCalledFunction()) {
				} else {
					errs() << "double call instruction not direct call\n";
				}
			} else if(PHINode *phi = dyn_cast<PHINode>(value)) {
				for(Value *phivalue : phi->incoming_values())
					solveRes.push_back(phivalue);
			} else if(Argument *arg = dyn_cast<Argument>(value)) {
				// TODO 分析返回值的时候需要细致到phi节点来自哪个基本块。把函数的到达数据流和参数的一起分析
				std::list<Function*> solveValueRes = solveValue(caller->getArgOperand(arg->getArgNo()));
				tmp.splice(tmp.end(), solveValueRes);
			} else {
			}
			solveRes.pop_front();
		}
		return tmp;
	}

	std::list<Function*> solveValue(Value *origin) {
		std::list<Function*> tmp;
		std::list<Value*> solveRes;
		solveRes.push_back(origin);
		while(!solveRes.empty()) {
			Value *value = solveRes.front();
			if (Function *theFunc = dyn_cast<Function>(value)) {
				tmp.push_back(theFunc);
			} else if (CallInst *callInst = dyn_cast<CallInst>(value)) {
				if (Function *doubleCall = callInst->getCalledFunction()) {
					solveFunction(callInst, doubleCall);
				} else {
					std::list<Function*> solveTheVal = solveValue(callInst->getCalledOperand());
					for (Function *func : solveTheVal) {
						tmp.splice(tmp.end(), solveFunction(callInst, func));
					}
					errs() << "double call instruction not direct call\n";
				}
			} else if(PHINode *phi = dyn_cast<PHINode>(value)) {
				for(Value *phivalue : phi->incoming_values())
					solveRes.push_back(phivalue);
			} else if(Argument *arg = dyn_cast<Argument>(value)) {
				for(User *user : arg->getParent()->users())
					if (CallInst *callInst = dyn_cast<CallInst>(user)) {
						solveRes.push_back(callInst->getArgOperand(arg->getArgNo()));
					} else {
						errs() << "for argument user, not a call instruction!\n";
					}
			} else {
			}
			solveRes.pop_front();
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

    /// Your pass to print Function and Call Instructions
    Passes.add(new FuncPtrPass());
    Passes.run(*M.get());
}
