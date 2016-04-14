#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "llvm/IR/InstIterator.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/StringMap.h"
using namespace llvm;

#define DEBUG_TYPE "copy_paste"

// Useful shit
// http://llvm.org/docs/ProgrammersManual.html#iterating-over-the-basicblock-in-a-function

StringRef get_function_name(CallInst *call)
{
    StringRef args("");
    // for (op_iterator itr = call->arg_begin(), E = call->arg_end(); itr != E; ++itr) {
    //   args << *itr << ", ";
    // }
    // args = args.rtrim(", ");

    Function *fun = call->getCalledFunction();
    if (fun) // thanks @Anton Korobeynikov
        return fun->getName() ; // inherited from llvm::Value
    else
        return StringRef("indirect call");
}

namespace {
  struct SkeletonPass : public FunctionPass {
  private:
    StringMap<int> checkedCache;

    std::string getKey(inst_iterator it) {
      BasicBlock::iterator itr(*it);
      return getKey(itr);
    }

    std::string getKey(BasicBlock::iterator it) {
      CallInst* callInst = dyn_cast<CallInst>(&*it);
      if (!callInst) {
        return "-1";
      }
      const DebugLoc &location = it->getDebugLoc();

      Twine id(get_function_name(callInst));

      if (location) {
        Twine parms(" @ " + location->getFilename() + ":" + Twine(location.getLine()) + ", col " + Twine(location.getCol()));
        Twine key(id + parms);
        return key.str();
      }
      return id.str();
    }

  public:
    static char ID;
    SkeletonPass() : FunctionPass(ID) { }

    virtual bool runOnFunction(Function &F) {

      // For every instruction in the function
      for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
        DEBUG(errs() << "Current instruction:" << *I << "\n");


        // This is the juicy part..
        BasicBlock::iterator it(*I);

        // Check if the line has already been checked previously
        std::string cacheKey = getKey(it);
        if (cacheKey != "-1" && checkedCache[cacheKey]) {
          DEBUG(errs() << "Instruction has already been found to be possible error. Skipping" << "\n");
          continue;
        }

        ++it; // After this line, it refers to the instruction after *I

        bool isFirst = true;
        bool hasError = false;
        bool isSame = false;
        bool isIdent = false;

        // Lets check the rest of the program..
        while (it != I->getParent()->end()) {
          errs() << "Up next:" << *it << "\n";
          hasError = false;
          isSame = false;
          isIdent = false;

          // Check for identical call
          if (I->isIdenticalTo(it)) {
            isIdent = true;
            hasError = true;
          }
          // Check if its a similar call
          else if (I->isSameOperationAs(it)) {
            // Op codes matches..
            hasError = true;
            isSame = true;
          }

          if (hasError) {
            CallInst* callInst = dyn_cast<CallInst>(&*I);
            CallInst* callInst2 = dyn_cast<CallInst>(&*it);

            // const DebugLoc &location = I->getDebugLoc();
            // const DebugLoc &location2 = it->getDebugLoc();

            if (callInst && callInst2) {
              if (isFirst) {
                errs() << "Original call: " << getKey(I) << "\n";
              }

              errs() << "\tPossble paste error: " << getKey(it) << "\n";
              if (isIdent) {
                errs() << "\t- Identical method found. Possbile copy/paste\n";
              }
              if (isSame) {
                errs() << "\t- Same operation found. Needs deeper check\n";
                errs() << callInst->getNumArgOperands() << ", " << callInst2->getNumArgOperands() << "\n";

                //CallInst* pCall   pCall is a printf called in my situation
                // if( ConstantExpr * pCE = dyn_cast<ConstantExpr>( pCall->getArgOperand(0))
                //   && ConstantExpr * pCE2 = dyn_cast<ConstantExpr>( pCall->getArgOperand(0))){

                //   if( GlobalVariable * pGV = dyn_cast<GlobalVariable>( pCE->getOperand(0))){

                //     if( ConstantArray * pCA = dyn_cast<ConstantArray>(
                //                                   pGV->getInitializer()
                //                                   )){
                //         Err << pCA->getAsString() << "\n";
                //      }
                //   }

                // }

              }

              checkedCache[getKey(it)] = 1;
              if (isFirst) {
                isFirst = false;
              }
            }
          }

          it++;
        }
      }

      return false;
    }

  };
}

char SkeletonPass::ID = 0;

// Automatically enable the pass.
// http://adriansampson.net/blog/clangpass.html
static void registerSkeletonPass(const PassManagerBuilder &,
                         legacy::PassManagerBase &PM) {
  PM.add(new SkeletonPass());
}
static RegisterStandardPasses
  RegisterMyPass(PassManagerBuilder::EP_EarlyAsPossible,
                 registerSkeletonPass);
