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

	int line = 0;
	std::list<std::string> names;
	std::map<int, std::list<std::string>> result;

    bool runOnModule(Module &M) override {
		bool res = false;
		Module *tmp = &M;
		for (Module::iterator ff = tmp->begin(); ff != tmp->end(); ++ff)
			for (Function::iterator bb = ff->begin(); bb != ff->end(); ++bb)
				for (BasicBlock::iterator ii = bb->begin(); ii != bb->end(); ++ii)
					res |= checkCallInst(dyn_cast<CallInst>(ii));
		for (std::map<int, std::list<std::string>>::iterator it = result.begin(); it != result.end(); ++it){
			errs() << it->first << " : ";
			for (std::list<std::string>::iterator name = it->second.begin(); name != it->second.end(); ++name)
				errs() << *name << ' ';
			errs() << '\n';
		}
        return false;
    }

	bool checkCallInst(CallInst *callInst) {
		if(!callInst)return false;
		this->line = callInst->getDebugLoc().getLine();
		if(Function *func = callInst->getCalledFunction()){	// 最简单的直接函数调用
			names.push_back(func->getName());
		}else if(Value *value = callInst->getCalledOperand()){
			checkPhiNode(dyn_cast<PHINode>(value));
		}
		result[this->line] = std::move(this->names);
		return false;
	}

	void checkPhiNode(PHINode *node) {
		if(!node)return;
		auto range = node->incoming_values();
		for (PHINode::op_iterator it = range.begin(); it != range.end(); ++it)
			if (Function *funcInPhi = dyn_cast<Function>(it->get()))
				names.push_back(funcInPhi->getName());
			else if(PHINode *newnode = dyn_cast<PHINode>(it->get()))
				checkPhiNode(newnode);
			else if(Argument *arg = dyn_cast<Argument>(it->get()))
				errs() << "still unimplement it !\n";
			else
				errs() << "somethin you never take account\n";
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
