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

	CallInst* currentInst;
	std::list<Function*> functions;
	std::map<CallInst*, std::list<Function*>> result;	// call指令对应到哪些函数
	std::map<Function*, std::list<CallInst*>> functionMap;	// 已经知道被call指令调用到的函数，都在哪些指令中被调用
	std::map<CallInst*, std::list<Argument*>> argumentMap; // 对于一个直接调用传入参数的call,有哪些参数可能被调用

    bool runOnModule(Module &M) override {
		bool res = false;
		Module *tmp = &M;
		for (Module::iterator ff = tmp->begin(); ff != tmp->end(); ++ff)
			for (Function::iterator bb = ff->begin(); bb != ff->end(); ++bb)
				for (BasicBlock::iterator ii = bb->begin(); ii != bb->end(); ++ii)
					res |= checkCallInst(dyn_cast<CallInst>(ii));
		for (std::map<CallInst*, std::list<Argument*>>::iterator it = argumentMap.begin(); it != argumentMap.end(); ++it) {
			currentInst = dyn_cast<CallInst>(it->first);
			std::list<Argument*> arguments = std::move(it->second);
			while(!arguments.empty()){
				Argument *arg = arguments.front();
				Function *func = arg->getParent();
				for (std::list<CallInst*>::iterator instIt = functionMap[func].begin(); instIt != functionMap[func].end(); ++instIt) {
					Value* value = (*instIt)->getArgOperand(arg->getArgNo());
					if (Function *theFunction = dyn_cast<Function>(value)){
						result[currentInst].push_back(theFunction);
					}
					checkPhiNode(dyn_cast<PHINode>(value));
				}
				arguments.pop_front();
			}
		}
			
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
		currentInst = callInst;
		if(Function *func = callInst->getCalledFunction()){	// 最简单的直接函数调用
			result[currentInst].push_back(func);
			functionMap[func].push_back(currentInst);
		}else if(Value *value = callInst->getCalledOperand()){
			checkPhiNode(dyn_cast<PHINode>(value));
			checkArgument(dyn_cast<Argument>(value));
		}
		return false;
	}

	void checkPhiNode(PHINode *node) {
		if(!node)return;
		auto range = node->incoming_values();
		for (PHINode::op_iterator it = range.begin(); it != range.end(); ++it)
			if (Function *funcInPhi = dyn_cast<Function>(it->get())){
				errs() << "get a function " << funcInPhi->getName() << '\n';
				result[currentInst].push_back(funcInPhi);
				functionMap[funcInPhi].push_back(currentInst);
			}else if(PHINode *newnode = dyn_cast<PHINode>(it->get()))
				checkPhiNode(newnode);
			else if(Argument *arg = dyn_cast<Argument>(it->get())){
				argumentMap[currentInst].push_back(arg);
				errs() << "still unimplement it !\n";
			}else
				errs() << "something you never take account\n";
	}

	void checkArgument(Argument *arg) {
		if(!arg)return;
		argumentMap[currentInst].push_back(arg);
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
