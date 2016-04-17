#include <unordered_set>

#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Constants.h"

#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/CallSite.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "llvm/IR/InstIterator.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/StringMap.h"
using namespace llvm;

// http://stackoverflow.com/questions/216823/whats-the-best-way-to-trim-stdstring
#include <algorithm> 
#include <functional> 
#include <cctype>
#include <locale>

// trim from start
static inline std::string &ltrim(std::string &s) {
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
  return s;
}

// trim from end
static inline std::string &rtrim(std::string &s) {
  s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
  return s;
}

// trim from both ends
static inline std::string &trim(std::string &s) {
  return ltrim(rtrim(s));
}

int edit_distance(const std::string& s1, const std::string& s2)
{
  const std::size_t len1 = s1.size(), len2 = s2.size();
  std::vector<std::vector<unsigned int>> d(len1 + 1, std::vector<unsigned int>(len2 + 1));

  d[0][0] = 0;
  for(unsigned int i = 1; i <= len1; ++i) d[i][0] = i;
  for(unsigned int i = 1; i <= len2; ++i) d[0][i] = i;

  for(unsigned int i = 1; i <= len1; ++i)
    for(unsigned int j = 1; j <= len2; ++j)
                      // note that std::min({arg1, arg2, arg3}) works only in C++11,
                      // for C++98 use std::min(std::min(arg1, arg2), arg3)
                      d[i][j] = std::min({ d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + (s1[i - 1] == s2[j - 1] ? 0 : 1) });
  return d[len1][len2];
}

int MAX_EDIT_DIST = 2;

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

    StringRef getFunctionName(inst_iterator it) {
      CallInst* callInst = dyn_cast<CallInst>(&*it);
      if (!callInst) {
        return "-1";
      }
      return get_function_name(callInst);
    }

    int getFunctionLine(inst_iterator it) {
      const DebugLoc &location = it->getDebugLoc();
      if (!location) {
        return -1;
      }
      return location.getLine();
    }

    int getFunctionLine(Instruction * I) {
      const DebugLoc &location = I->getDebugLoc();
      if (!location) {
        return -1;
      }
      return location.getLine();
    }

  public:
    static char ID;
    SkeletonPass() : FunctionPass(ID) { }

    virtual bool runOnFunction(Function &F) {

      std::unordered_set<std::string> visited;

      for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
        // Get the current method name to be called
        StringRef funcName = getFunctionName(I);

        if (visited.count(funcName.str()) > 0) {
          // We already evaluated this function name
          // errs() << "Skipping\n";
          continue;
        }

        visited.insert(funcName.str());

        int funcLine = getFunctionLine(I);
        errs() << "Method: " << funcName << " on line " << funcLine << "\n";
 
        // If its not a direct call
        // A delcaration triggers this as well
        if (funcName != "-1") {
          Module * m = F.getParent();
          Function * f = m->getFunction(funcName);

          errs() << "Function f: " << f << "\n";

          // Get all the uses of this method call (including this one)
          for (User *U : f->users()) {
            if (Instruction *Inst = dyn_cast<Instruction>(U)) {
              int curLine = getFunctionLine(Inst);
              if (curLine == funcLine) {
                // This is the same line as we originate from..
                continue;
              }

              if (I->isIdenticalTo(Inst)) {
                errs() << "Identical call on line " << curLine << "\n";
              } else {
                
                // Inspect arguments
                errs() << "Same call: " << curLine << "\n";
                errs() << *Inst << "\n";

                // http://stackoverflow.com/questions/12127137/how-to-get-the-value-of-a-string-literal-in-llvm-ir
                CallSite CSI(cast<Value>(&*I));
                CallSite CSInst(cast<Value>(Inst));
                if (!CSI || !CSInst) {
                  errs() << "Couldn't get CallSite\n";
                  continue;
                }

                int numOperI = CSI.arg_size();
                // errs() << "Arguments for I: " << numOperI << "\n";
                int numOperInst = CSInst.arg_size();
                // errs() << "Arguments for Inst: " << numOperInst << "\n";

                if (numOperI == numOperInst) {
                  for (int i = 0; i < numOperInst; ++i) {
                    errs() << "TYPE: " << I->getOperand(i)->getType()->getTypeID() << "\n";

                    if (I->getOperand(i)->getType()->getTypeID() == Type::TypeID::ArrayTyID) {
                      // Start "Array" (String) analysis

                      // Trim for newlines, as its very common in C / C++
                      std::string argI = cast<ConstantDataArray>(
                        cast<GlobalVariable>(
                          cast<ConstantExpr>(
                            CSI.getArgument(i)
                          )->getOperand(0)
                        )->getInitializer()
                      )->getAsCString();
                      argI = trim(argI);

                      std::string argInst = cast<ConstantDataArray>(
                        cast<GlobalVariable>(
                          cast<ConstantExpr>(
                            CSInst.getArgument(i)
                          )->getOperand(0)
                        )->getInitializer()
                      )->getAsCString();
                      argInst = trim(argInst);

                      errs() << argI << " vs " << argInst << "\n";
                      int dist = edit_distance(argI, argInst);
                      errs() << "Levenshtein: " << dist << "\n";
                      int pos;
                      if (argI == argInst) {
                        // Why isnt this identical? A second argument is different?
                        errs() << "Identical\n";
                      } else if (argI.find(argInst) != std::string::npos) {
                        // argInst is a substring of argI
                        // i.e. something was removed
                        errs() << "#00'" << argInst << "' is a substring of '" << argI << "'.\n";
                        pos = argI.find(argInst);
                        if (pos == 0) {
                          errs() << "#1 Did you intend to remove '" << argI.substr(argInst.length(), std::string::npos) << "' from the end?\n";
                        } else {
                          if (argInst.length() + pos == argI.length()) {
                            // Only added something in front
                            errs() << "#2 Did you intend to add '" << argI.substr(0, pos) << "' in the begining?\n";
                          } else {
                            errs() << "#3 Did you intend to add '" << argI.substr(0, pos) << "' in the begining and '";
                            errs() << argI.substr(pos + argInst.length(), std::string::npos) << "' in the end?\n";
                          }
                        }
                      } else if (argInst.find(argI) != std::string::npos) {
                        // And the other way around
                        // i.e. something was added
                        errs() << "#01'" << argI << "' is a substring of '" << argInst << "'.\n";
                        pos = argInst.find(argI);
                        if (pos == 0) {
                          errs() << "#4 Did you intend to remove '" << argInst.substr(argI.length(), std::string::npos) << "' from the end?\n";
                        } else {
                          if (argI.length() + pos == argInst.length()) {
                            // Only added something in front
                            errs() << "#5 Did you intend to add '" << argInst.substr(0, pos) << "' in the begining?\n";
                          } else {
                            errs() << "#6 Did you intend to add '" << argInst.substr(0, pos) << "' in the begining and '";
                            errs() << argInst.substr(pos + argI.length(), std::string::npos) << "' in the end?\n";
                          }
                        }
                      } else if (dist < MAX_EDIT_DIST) {
                        // Levenstein distance
                        errs() << "Levenshtein distance of " << dist << " between:\n";
                        errs() << "   '" << argI << "'\n";
                        errs() << "  and\n";
                        errs() << "   '" << argInst << "'\n";
                        errs() << "Is this intended?";
                      }
                    } // End "Array" (Strings) analysis
                    else if (I->getOperand(i)->getType()->getTypeID() == Type::TypeID::ArrayTyID) {
                      // Start "Metadata" (Delclaration?) analysis

                    } // End "Metadata" (Delclaration?) analysis

                  }

                } else {
                  // Check for mistakes in numOper?
                }
                
                // Inst->isSameOperationAs.. for a*a?
              }
            }
          }

          // for (Value::use_iterator i = f->use_begin(), e = f->use_end(); i != e; ++i) {
          //   errs() << "  " << *i << "\n";
          //   if (Instruction *Inst = dyn_cast<Instruction>(*i)) {
          //     errs() << "f is used in instruction:\n";
          //     errs() << *Inst << "\n";
          //   }
          // }
        } else {
          // A direct call..
          // i.e. right hand side of: int b = 2+4;
        }
        // f.begin() -> f.end() to loop over BasicBlocks (usage of method?)
        // Debug print: errs() << getKey(it) << "\n";
        // Use CallInst->getArgOperand(0..n) to compare args
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
