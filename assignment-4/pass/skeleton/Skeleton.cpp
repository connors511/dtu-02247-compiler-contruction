#include <unordered_set>

#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"

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

// Hamming distance
// http://stackoverflow.com/a/557436
int hammingDist(std::string const& s1, std::string const& s2)
{
    // hd stands for "Hamming Distance"
    int dif = 0;

    for (std::string::size_type i = 0; i < s1.size(); i++ )
    {
        char b1 = s1[i];
        char b2 = s2[i];

        dif += (b1 != b2)?1:0;
    }

    return dif;
}

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

    // Get variable name
    // http://stackoverflow.com/a/21488105
    // With https://groups.google.com/forum/#!topic/llvm-dev/RRV2TjNQ8oA applied
    const Function* findEnclosingFunc(const Value* V) {
      if (const Argument* Arg = dyn_cast<Argument>(V)) {
        return Arg->getParent();
      }
      if (const Instruction* I = dyn_cast<Instruction>(V)) {
        return I->getParent()->getParent();
      }
      return NULL;
    }

    const DIVariable* findVar(const Value* V, const Function* F) {
      for (const_inst_iterator Iter = inst_begin(F), End = inst_end(F); Iter != End; ++Iter) {
        const Instruction* I = &*Iter;
        if (const DbgDeclareInst* DbgDeclare = dyn_cast<DbgDeclareInst>(I)) {
          if (DbgDeclare->getAddress() == V) return DbgDeclare->getVariable();
        } else if (const DbgValueInst* DbgValue = dyn_cast<DbgValueInst>(I)) {
          if (DbgValue->getValue() == V) return DbgValue->getVariable();
        }
      }
      return NULL;
    }

    StringRef getOriginalName(const Value* V) {
      // TODO handle globals as well

      const Function* F = findEnclosingFunc(V);
      if (!F) return V->getName();

      const DIVariable* Var = findVar(V, F);
      if (!Var) return "tmp";

      return Var->getName();
    }

    StringRef getVariableName(Value * V) {
      LoadInst *LI = cast<LoadInst>(V);
      Value *VaI = LI->getPointerOperand();
      return VaI->getName();
    }

    StringRef getVariableName(Instruction * I) {
      return getVariableName(I, 0);
    }

    StringRef getVariableName(Instruction * I, int operand) {
      return getVariableName(I->getOperand(operand));
    }

    Value * getVariableAddress(Instruction * I) {
      LoadInst *LI = cast<LoadInst>(I->getOperand(0));
      Value *VaI = LI->getPointerOperand();
      return VaI;
    }

    // std::string _processExpression(store) {
    //   std::string sequence;
    //   return _visit(sequence, store.value);
    // }

    // std::string _visit(std::string &sequence, Instruction * I) {
    //   if (isa<LoadInst>(I)) {
    //     sequence += getVariableName(I);
    //   } else if (value is binaryop) {
    //     // sequence.add(openingParen)
    //     _visit(sequence, binaryop.operand(0))
    //     sequence += I.getOpcode();
    //     _visit(sequence, binaryop.operand(1))
    //     // sequence.add(closingParen);
    //   }
    //   return sequence;
    // }

    std::string instructionToText(Instruction * I) {
      if (isa<LoadInst>(I)) {
        return getVariableName(I);
      }
      std::string str;
      raw_string_ostream rso(str);
      I->print(rso);
      return str;
    }

    std::string valueToText(Value * V) {
      if (isa<LoadInst>(V)) {
        return getVariableName(V);
      }
      std::string str;
      raw_string_ostream rso(str);
      V->print(rso);
      return str;
    }

  public:
    static char ID;
    SkeletonPass() : FunctionPass(ID) { }

    virtual bool runOnFunction(Function &F) {
      /********************************************************************\
       *                                                                  *
       * WE REQUIRE TO RUN WITH DEBUG (-g flag)                           *
       * $ clang -Xclang -load -Xclang skeleton/libSkeletonPass.* FILE -g *
       *                                                                  *
      \********************************************************************/
      std::unordered_set<std::string> visited;

      for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
        // Get the current method name to be called
        StringRef funcName = getFunctionName(I);

        if (visited.count(funcName.str()) > 0) {
          // We already evaluated this function name
          // errs() << "Skipping " << funcName << " on " << getFunctionLine(I) <<" \n";
          continue;
        }

        visited.insert(funcName.str());

        int funcLine = getFunctionLine(I);
        // errs() << "Method: " << funcName << " on line " << funcLine << "\n";

        // If its not a direct call
        if (funcName != "-1") {
          Module * m = F.getParent();
          Function * f = m->getFunction(funcName);

          // errs() << "Function f: " << f << "\n";

          int sameArgsCount = 0;
          int diffArgsCount = 0;
          std::string errMsg;

          // Get all the uses of this method call (including this one)
          for (User *U : f->users()) {
            if (Instruction *Inst = dyn_cast<Instruction>(U)) {
              int curLine = getFunctionLine(Inst);
              if (curLine == funcLine) {
                // This is the same line as we originate from..
                continue;
              }

              if (I->isIdenticalTo(Inst)) {
                // errs() << "Identical call on line " << curLine << "\n";
                errs() << "Identical statements (";
                errs() << funcName.str();
                errs() << ") found on lines " << funcLine << " and " << curLine << ". Did you mean to?\n";
              } else {

                // Inspect arguments
                // errs() << "Same call: " << curLine << "\n";

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

                // If Inst is @llvm.dbg.declare:
                // - The first argument is metadata holding the alloca for the variable.
                // - The second argument is a local variable containing a description of the variable. http://llvm.org/docs/LangRef.html#dilocalvariable
                // - The third argument is a complex expression. http://llvm.org/docs/LangRef.html#diexpression

                if (numOperI == numOperInst) {
                  for (int i = 0; i < numOperInst; ++i) {
                    // errs() << "TYPE: " << I->getOperand(i)->getType()->getTypeID() << "\n";

                    if (I->getOperand(i)->getType()->getTypeID() == 14) { // Type::TypeID::ArrayTyID
                      // Start "Array" (String) analysis

                      std::string argI = cast<ConstantDataArray>(
                        cast<GlobalVariable>(
                          cast<ConstantExpr>(
                            CSI.getArgument(i)
                          )->getOperand(0)
                        )->getInitializer()
                      )->getAsCString();
                      // Trim for newlines, as its very common in C / C++
                      argI = trim(argI);

                      std::string argInst = cast<ConstantDataArray>(
                        cast<GlobalVariable>(
                          cast<ConstantExpr>(
                            CSInst.getArgument(i)
                          )->getOperand(0)
                        )->getInitializer()
                      )->getAsCString();
                      argInst = trim(argInst);

                      // errs() << argI << " vs " << argInst << "\n";
                      int dist = edit_distance(argI, argInst);
                      // errs() << "Levenshtein: " << dist << "\n";
                      int pos;
                      if (argI == argInst) {
                        // Why isnt this identical? A second argument is different?
                        sameArgsCount++;
                        // errs() << "Identical\n";
                      } else {
                        // Deeper analysis
                        diffArgsCount++;

                        if (argI.find(argInst) != std::string::npos) {
                          // argInst is a substring of argI
                          // i.e. something was removed
                          errs() << "On line " + std::to_string(curLine) + " argument #" + std::to_string(i+1) + " differs ";
                          errs() << "from first sight on line " + std::to_string(funcLine) + ":\n";
                          errs() << "   '" << argInst << "' is a substring of '" << argI << "'.\n";
                          pos = argI.find(argInst);
                          if (pos == 0) {
                            errs() << " Did you intend to remove '" << argI.substr(argInst.length(), std::string::npos) << "' from the end?\n";
                          } else {
                            if (argInst.length() + pos == argI.length()) {
                              // Only added something in front
                              errs() << " Did you intend to add '" << argI.substr(0, pos) << "' in the begining?\n";
                            } else {
                              errs() << " Did you intend to add '" << argI.substr(0, pos) << "' in the begining and '";
                              errs() << argI.substr(pos + argInst.length(), std::string::npos) << "' in the end?\n";
                            }
                          }
                        } else if (argInst.find(argI) != std::string::npos) {
                          // And the other way around
                          // i.e. something was added
                          errs() << "On line " + std::to_string(curLine) + " argument #" + std::to_string(i+1) + " differs ";
                          errs() << "from first sight on line " + std::to_string(funcLine) + ":\n";
                          errs() << "   '" << argI << "' is a substring of '" << argInst << "'.\n";
                          pos = argInst.find(argI);
                          if (pos == 0) {
                            errs() << " Did you intend to add '" << argInst.substr(argI.length(), std::string::npos) << "' to the end?\n";
                          } else {
                            if (argI.length() + pos == argInst.length()) {
                              // Only added something in front
                              errs() << " Did you intend to remove '" << argInst.substr(0, pos) << "' from the begining?\n";
                            } else {
                              errs() << " Did you intend to remove '" << argInst.substr(0, pos) << "' from the begining and '";
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
                      }
                    } // End "Array" (Strings) analysis
                    else if (I->getOperand(i)->getType()->getTypeID() == Type::TypeID::MetadataTyID) {
                      // Start "Metadata" (Delclaration?) analysis

                      // errs() << "Line: " << curLine << "\n";
                      // const Value * V = &*I->getOperand(i);
                      // errs() << "V: " << (*V) << "\n";
                      // // DILocalVariable DILV = *V;
                      // errs() << "V raw: " << (V) << "\n";
                      // errs() << "NAME: " << getOriginalName(V).data() << "\n";

                    } // End "Metadata" (Delclaration?) analysis
                    else if (I->getOperand(i)->getType()->getTypeID() == 10) { // Type::TypeID::TokenTyID) {
                      // Start "Token" (Variables in method calls?) analysis

                      CallInst* ciI = dyn_cast<CallInst>(&*I);
                      CallInst* ciInst = dyn_cast<CallInst>(&*Inst);
                      ConstantInt* CI = dyn_cast<llvm::ConstantInt>(&*I->getOperand(i));

                      if (isa<LoadInst>(&*I->getOperand(i)) && isa<LoadInst>(Inst->getOperand(i))
                            && getVariableName(&*I, i) == getVariableName(Inst, i)) {
                        // Both arguments are simple literals
                        sameArgsCount++;
                        // errs() << "ON LINE " << curLine << ", operand " << i << " IS EQUAL\n";
                      } else if (ConstantInt* CInst = dyn_cast<llvm::ConstantInt>(Inst->getOperand(i))) {
                        if (CI && CInst->getSExtValue() == CI->getSExtValue()) {
                          // Same args
                          sameArgsCount++;
                        } else if (CI && CInst->getSExtValue() != CI->getSExtValue()) {
                          errMsg += "On line " + std::to_string(curLine) + " argument #" + std::to_string(i+1) + " differs ";
                          errMsg += "from first sight on line " + std::to_string(funcLine) + ":\n";
                          errMsg += "   " + std::to_string(CInst->getSExtValue()) + " (" + std::to_string(CInst->getBitWidth()) + "bit int)\n";
                          errMsg += " instead of\n";
                          errMsg += "   " + std::to_string(CI->getSExtValue())  + " (" + std::to_string(CI->getBitWidth()) + "bit int)\n";
                          errMsg += " Is this intended?\n";
                        } else {
                          errMsg += "On line " + std::to_string(curLine) + " argument #" + std::to_string(i+1) + " differs ";
                          errMsg += "from first sight on line " + std::to_string(funcLine) + ":\n";
                          errMsg += "   " + std::to_string(CInst->getSExtValue()) + " (" + std::to_string(CInst->getBitWidth()) + "bit int)\n";
                          errMsg += " instead of\n";
                          if (CI) {
                            errMsg += "   " + std::to_string(CI->getSExtValue())  + " (" + std::to_string(CI->getBitWidth()) + "bit int)\n";
                          } else {
                            errMsg += "   " + valueToText(I->getOperand(i)) + "\n";
                          }
                          errMsg += " Is this intended?\n";
                        }
                      } else {
                        // TODO: Handle complex expressions.. a*a or a+b for example.

                        errMsg += "On line " + std::to_string(curLine) + " argument #" + std::to_string(i+1) + " differs ";
                        errMsg += "from first sight on line " + std::to_string(funcLine) + ":\n";
                        errMsg += "   " + valueToText(Inst->getOperand(i)) + "\n";
                        errMsg += " instead of\n";
                        if (CI) {
                          errMsg += "   " + std::to_string(CI->getSExtValue())  + " (" + std::to_string(CI->getBitWidth()) + "bit int)\n";
                        } else {
                          errMsg += "   " + valueToText(I->getOperand(i)) + "\n";
                        }
                        errMsg += " Is this intended?\n";

                        // errs() << I->getOperand(i) << " : " << *I->getOperand(i) << "\n";
                        // errs() << Inst->getOperand(i) << " : " << *Inst->getOperand(i) << "\n";
                        // errs() << *ciI << "\n";
                        // errs() << *ciInst << "\n";
                        // errs() << "ON LINE " << curLine << ", operand " << i << " IS NOT EQUAL\n";

                        // errs() << "TEXT: " << instructionToText(Inst) << "\n";
                      }
                    } // End "Token" (Variables in method calls?) analysis
                  }
                } else {
                  // Check for mistakes in numOper?
                }

                // Compute the difference in argument names
                // We might need to both consider single arguments; "add(a, b)"
                // And complex types; "add(a*a, b)"
                errs() << errMsg;
                if (diffArgsCount == 0 && sameArgsCount == numOperI) {
                  // Identical call args
                  errs() << "Identical statements (";
                  errs() << funcName.str();
                  errs() << ") found on lines " << funcLine << " and " << curLine << ". Did you mean to?\n";
                } else if (diffArgsCount == 1 && numOperI > 2) {
                  // Did you mean to?
                  // You might've miss spelled it..
                  // errs() << "ALMOST" << "\n";
                } else if (sameArgsCount == 1 && numOperI > 2) {
                  // We should reverse the error message.
                  // You probably forgot to change the last argument
                  // errs() << "REVERSE ERR\n";
                } else {
                  // We're guessing this is intended
                }

                // Inst->isSameOperationAs.. for a*a?
                // -- Seems to return the instruction type; load, store, call etc
              }
            }
          }
        } else {
          // A direct call..
          // i.e. right hand side of: int b = 2+4;
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
