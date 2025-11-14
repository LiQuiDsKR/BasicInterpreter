#include <stdio.h>    // 표준 입출력 라이브러리 (printf, fopen 등)
#include <conio.h>    // getch() 사용 라이브러리 (Windows 콘솔)
#include <string.h>   // 문자열 처리 라이브러리 (strcpy, strcmp, strtok)
#include <stdlib.h>   // 동적 메모리 할당 라이브러리 (mallo, free)
#include <ctype.h>    // 문자 판별 관련 (isdigit, isalpha 등)

#ifdef _WIN32                   // OS 판단 (콘솔 초기화 명령이 서로 다르기 때문)
#define CLEAR() system("cls")   // 윈도우면 cls 명령으로 콘솔 지우기
#else
#define CLEAR() system("clear") // 윈도우가 아니라면 clear 명령으로 콘솔 지우기
#endif

// 프로그램 내에서 사용되는 '노드' 구조체 정의
// 스택에 저장되는 하나의 요소(변수, 함수, 호출, begin/end 등)
struct node {
    int type;           // 1: 변수, 2: 함수(정의), 3: 함수호출, 4: begin(중괄호 Open의 역할), 5: end(중괄호 Close의 역할)
    char exp_data;      // 변수명 혹은 함수명 (Char, 즉 이 코드에서는 변수나 함수 이름의 크기는 1Byte로 제한됨)
    int val;            // 값 (정수)
    int line;           // 함수의 라인 번호 (호출시 돌아가기 위함)
    struct node* next;  // 다음 노드 (스택 연결)
};
typedef struct node Node;

// 스택 Node
struct stack { Node* top; };
typedef struct stack Stack;

// 연산자 스택 Node
struct opnode { char op; struct opnode* next; };
typedef struct opnode opNode;

// 연산자 스택
struct opstack { opNode* top; };
typedef struct opstack OpStack;

// 후위표기 계산 Node
struct postfixnode { int val; struct postfixnode* next; };
typedef struct postfixnode Postfixnode;

// 후위표기 스택
struct postfixstack { Postfixnode* top; };
typedef struct postfixstack PostfixStack;

// Interpreter에서 사용하는 static 함수들 미리 선언
static int GetVal(char, int*, Stack*);
static int GetLastFunctionCall(Stack*);
static Stack* FreeAll(Stack*);
static int my_stricmp(const char* a, const char* b);
static void rstrip(char* s);

// Node를 스택에 푸시하는 함수
static Stack* Push(Node sNode, Stack* stck)
{
    Node* newnode = (Node*)malloc(sizeof(Node));     // 새 노드 동적할당
    if (!newnode) { printf("ERROR, Couldn't allocate memory..."); return NULL; }

    // 전달받은 sNode의 필드(argument)를 복사
    newnode->type = sNode.type;
    newnode->val = sNode.val;
    newnode->exp_data = sNode.exp_data;
    newnode->line = sNode.line;

    // 새 노드를 기존 스택 top 위에 연결
    newnode->next = stck->top;
    stck->top = newnode;
    return stck;
}

// 연산자를 연산자 스택(OpStack)에 푸시
static OpStack* PushOp(char op, OpStack* opstck)
{
    opNode* newnode = (opNode*)malloc(sizeof(opNode));
    if (!newnode) { printf("ERROR, Couldn't allocate memory..."); return NULL; }
    newnode->op = op;
    newnode->next = opstck->top;
    opstck->top = newnode;
    return opstck;
}

// OpStack에서 연산자 Pop 및 메모리 해제
static char PopOp(OpStack* opstck)
{
    opNode* temp;
    char op;
    if (opstck->top == NULL) // 공백 스택 감지
    {
        return 0;  // 스택이 비어있으면 0 반환
    }
    op = opstck->top->op;      // 맨 위 연산자 복사
    temp = opstck->top;        // 삭제용 포인터 저장
    opstck->top = opstck->top->next;
    free(temp);                // 메모리 해제
    return op;
}

// PostfixStack에 값 푸시
static PostfixStack* PushPostfix(int val, PostfixStack* poststck)
{
    Postfixnode* newnode = (Postfixnode*)malloc(sizeof(Postfixnode));
    if (!newnode) { printf("ERROR, Couldn't allocate memory..."); return NULL; }
    newnode->val = val;
    newnode->next = poststck->top;
    poststck->top = newnode;
    return poststck;
}

// PostfixStack에서 값 노드 Pop
static int PopPostfix(PostfixStack* poststck)
{
    Postfixnode* temp;
    int val;
    if (poststck->top == NULL)
    {
        return 0; // 비어있으면 0
    }
    val = poststck->top->val;
    temp = poststck->top;
    poststck->top = poststck->top->next;
    free(temp);
    return val;
}

// 일반 Stack에서 Node를 Pop, 값을 sNode로 반환
static void Pop(Node* sNode, Stack* stck)
{
    Node* temp;
    if (stck->top == NULL) return;
    sNode->exp_data = stck->top->exp_data;
    sNode->type = stck->top->type;
    sNode->line = stck->top->line;
    sNode->val = stck->top->val;
    temp = stck->top;
    stck->top = stck->top->next;
    free(temp);
}

// 연산자스택 공백 체크 (스택 자체가 비어있는지)
static int isStackEmpty(OpStack* stck)
{
    return stck->top == 0;
}

// 연산자 우선순위 반환 함수 (곱셈, 나눗셈이 덧셈, 뺄셈보다 우위)
static int Priotry(char operator)
{
    if ((operator=='+') || (operator=='-')) return 1;
    else if ((operator=='/') || (operator=='*')) return 2;
    return 0;
}

// main 함수 (실제 작동순서 기술)
int main(int argc, char** argv)
{
    // 입력 파일에서 한 줄씩 읽어올 버퍼
    char line[4096];       

    // 임시로 문자열을 담아둘 버퍼 (특히 함수 호출 시, 파일을 되감을 때 사용됨)
    char dummy[4096];      

    // 원본 line을 수정하기 전에 백업해둘 공간 (예: strtok으로 잘려도 원본 유지용)
    char lineyedek[4096];  

    // 후위 표기(postfix) 형태로 변환된 식을 저장할 배열
    char postfix[4096];    

    // 현재 줄의 첫 단어를 가리킬 포인터 (예: "int", "function", "begin" 등)
    char* firstword;       

    // 후위표기 계산 시 사용할 임시 피연산자 변수 2개
    int val1; 
    int val2; 

    // 최근 계산된 식(expression)의 결과값을 저장
    int LastExpReturn = 0;         

    // 함수가 리턴한 값을 임시로 저장 (기본값 -999 = ‘없음’)
    int LastFunctionReturn = -999; 

    // 함수 호출 시 전달되는 인자(argument) 값
    int CalingFunctionArgVal = 0;  

    // 스택에 푸시할 때 사용하는 임시 노드 구조체
    Node tempNode;  

    // #############################################
    // 주요 스택 구조체 3개를 동적할당함
    // 각각 연산자 스택, 계산용 스택, 변수/함수 관리용 스택
    // #############################################
    OpStack* MathStack = (OpStack*)malloc(sizeof(OpStack));
    FILE* filePtr;  // 코드 파일 포인터
    PostfixStack* CalcStack = (PostfixStack*)malloc(sizeof(PostfixStack));
    int resultVal = 0;  // 연산 결과를 임시 저장
    Stack* STACK = (Stack*)malloc(sizeof(Stack));

    // 현재 읽고 있는 파일의 라인 번호
    int curLine = 0;     

    // main() 함수가 발견되었는지 여부 (이게 있어야 구문이 실행됨)
    int foundMain = 0;   

    // 함수 호출 중 점프(break) 발생을 제어하는 플래그
    int WillBreak = 0;   

    // 메모리 할당이 정상적으로 되었는지 검증
    if (!MathStack || !CalcStack || !STACK) {
        printf("Memory alloc failed\n");
        return 1; // 실패 시 프로그램 종료
    }

    // 스택 구조체 초기화 (모두 비어있는 상태)
    MathStack->top = NULL;
    CalcStack->top = NULL;
    STACK->top = NULL;

    // 콘솔을 깨끗이 지움 (OS별로 cls / clear)
    CLEAR(); 

    // 인자가 정확히 2개여야 함 (프로그램 이름 + 파일 경로)
    if (argc != 2)
    {
        printf("Incorrect arguments!\n");
        printf("Usage: %s <inputfile.spl>", argv[0]); // 사용법 안내
        return 1;
    }

    // 입력 파일 열기 (읽기 전용)
    filePtr = fopen(argv[1], "r");
    if (filePtr == NULL)
    {
        printf("Can't open %s. Check the file please", argv[1]);
        return 2; // 파일 열기 실패 시 종료
    }

    // #####################################
    // 파일의 모든 줄을 한 줄씩 읽으면서 해석 시작
    // #####################################
    while (fgets(line, 4096, filePtr))
    {
        int k = 0;

        // 탭 문자(\t)는 공백(' ')으로 치환
        while (line[k] != '\0')
        {
            if (line[k] == '\t') line[k] = ' ';
            k++;
        }

        // 개행 문자 제거 (rstrip: '\n', '\r', ' ' 제거)
        rstrip(line);

        // 원본 라인을 백업해둠 (strtok 등으로 수정될 수 있으므로)
        strcpy(lineyedek, line); 

        // 현재 라인 번호 증가
        curLine++;

        // tempNode 초기화 (기본값들로)
        tempNode.val = -999;
        tempNode.exp_data = ' ';
        tempNode.line = -999;
        tempNode.type = -999;

        // ##### 구문 분석 시작 #####

        // “begin” 키워드 처리
        if (my_stricmp("begin", line) == 0)
        {
            if (foundMain)
            {
                // main 함수 내부의 begin이면 스택에 push
                tempNode.type = 4; 
                STACK = Push(tempNode, STACK);
            }
        }

        // “end” 키워드 처리
        else if (my_stricmp("end", line) == 0)
        {
            if (foundMain)
            {
                int sline;
                // end 블록을 push
                tempNode.type = 5; 
                STACK = Push(tempNode, STACK);

                // 마지막 함수 호출의 라인 찾기
                sline = GetLastFunctionCall(STACK);

                // sline이 0이면 main() 함수의 끝이라는 뜻
                if (sline == 0)
                {
                    printf("Output=%d", LastExpReturn); 
                    // 프로그램의 최종 출력값 표시
                }
                else
                {
                    // 함수 호출에서 복귀하는 과정
                    int j;
                    int foundCall = 0;
                    LastFunctionReturn = LastExpReturn;

                    // 파일을 다시 처음부터 열어서
                    // 함수 호출 위치로 되감기
                    fclose(filePtr);
                    filePtr = fopen(argv[1], "r");
                    curLine = 0;
                    for (j = 1; j < sline; j++)
                    {
                        fgets(dummy, 4096, filePtr);
                        curLine++;
                    }

                    // 스택에서 함수 호출 노드 제거
                    while (foundCall == 0)
                    {
                        Pop(&tempNode, STACK);
                        if (tempNode.type == 3) foundCall = 1;
                    }
                }
            }
        }

        // "begin", "end"가 아니면 일반 명령문 처리
        else
        {
            // 한 줄의 첫 단어 추출
            firstword = strtok(line, " ");
            if (!firstword) continue; // 공백줄이면 무시

            // ##### 변수 선언 처리 #####
            if (my_stricmp("int", firstword) == 0)
            {
                if (foundMain) // main 이후만 해석
                {
                    tempNode.type = 1; // 변수 타입
                    firstword = strtok(NULL, " ");
                    if (!firstword) continue;
                    tempNode.exp_data = firstword[0]; // 변수명 (한 글자)

                    firstword = strtok(NULL, " ");
                    if (!firstword) continue;

                    // int a = 3 과 같은 경우 = 이후 값 파싱
                    if (my_stricmp("=", firstword) == 0)
                    {
                        firstword = strtok(NULL, " ");
                        if (!firstword) continue;
                    }

                    // 문자열을 정수로 변환하여 값 저장
                    tempNode.val = atoi(firstword);
                    tempNode.line = 0;
                    STACK = Push(tempNode, STACK);
                }
            }

            // ##### 함수 정의 처리 #####
            else if (my_stricmp("function", firstword) == 0)
            {
                firstword = strtok(NULL, " ");
                if (!firstword) continue;

                tempNode.type = 2;  // 함수 노드
                tempNode.exp_data = firstword[0]; // 함수명 첫 글자
                tempNode.line = curLine;          // 정의된 라인 번호
                tempNode.val = 0;
                STACK = Push(tempNode, STACK);

                // function main인 경우 실행 시작점 표시
                if (firstword[0] == 'm' && firstword[1] == 'a' && firstword[2] == 'i' && firstword[3] == 'n')
                {
                    foundMain = 1;
                }
                else
                {
                    // main이 아닌 함수는 인자 하나를 받는다고 가정
                    if (foundMain)
                    {
                        firstword = strtok(NULL, " ");
                        if (!firstword) continue;
                        tempNode.type = 1;
                        tempNode.exp_data = firstword[0];
                        tempNode.val = CalingFunctionArgVal; // 전달된 인자값
                        tempNode.line = 0;
                        STACK = Push(tempNode, STACK);
                    }
                }
            }

            // ##### 수식 처리 #####
            else if (firstword[0] == '(')
            {
                if (foundMain)
                {
                    int i = 0;
                    int y = 0;
                    MathStack->top = NULL; // 연산자 스택 초기화

                    // 중위표기 → 후위표기 변환 루프
                    while (lineyedek[i] != '\0')
                    {
                        // 숫자면 postfix에 바로 넣음
                        if (isdigit((unsigned char)lineyedek[i]))
                        {
                            postfix[y] = lineyedek[i];
                            y++;
                        }

                        // ')'를 만나면 연산자 pop
                        else if (lineyedek[i] == ')')
                        {
                            if (!isStackEmpty(MathStack))
                            {
                                postfix[y] = PopOp(MathStack);
                                y++;
                            }
                        }

                        // 연산자 (+, -, *, /)
                        else if (lineyedek[i] == '+' || lineyedek[i] == '-' || lineyedek[i] == '*' || lineyedek[i] == '/')
                        {
                            if (isStackEmpty(MathStack))
                            {
                                MathStack = PushOp(lineyedek[i], MathStack);
                            }
                            else
                            {
                                // 우선순위 비교하여 pop/push 결정
                                if (Priotry(lineyedek[i]) <= Priotry(MathStack->top->op))
                                {
                                    postfix[y] = PopOp(MathStack);
                                    y++;
                                    MathStack = PushOp(lineyedek[i], MathStack);
                                }
                                else
                                {
                                    MathStack = PushOp(lineyedek[i], MathStack);
                                }
                            }
                        }

                        // 알파벳: 변수나 함수 이름일 때
                        else if (isalpha((unsigned char)lineyedek[i]) > 0)
                        {
                            int codeline = 0;
                            int dummyint = 0;
                            int retVal = GetVal(lineyedek[i], &codeline, STACK);

                            // 변수면 값 바로 가져와 postfix에 추가
                            if ((retVal != -1) && (retVal != -999))
                            {
                                postfix[y] = (char)(retVal + 48);
                                y++;
                            }

                            // 함수 호출일 경우
                            else
                            {
                                if (LastFunctionReturn == -999)
                                {
                                    int j;
                                    tempNode.type = 3; // 함수 호출 기록
                                    tempNode.line = curLine;
                                    STACK = Push(tempNode, STACK);

                                    // 괄호 안 인자값 계산
                                    CalingFunctionArgVal = GetVal(lineyedek[i + 2], &dummyint, STACK);

                                    // 파일 다시 열어서 함수 정의 부분으로 점프
                                    fclose(filePtr);
                                    filePtr = fopen(argv[1], "r");
                                    curLine = 0;
                                    for (j = 1; j < codeline; j++)
                                    {
                                        fgets(dummy, 4096, filePtr);
                                        curLine++;
                                    }

                                    WillBreak = 1;
                                    break;
                                }
                                else
                                {
                                    // 이전 함수의 리턴값을 다시 사용
                                    postfix[y] = (char)(LastFunctionReturn + 48);
                                    y++;
                                    i = i + 3;
                                    LastFunctionReturn = -999;
                                }
                            }
                        }
                        i++;
                    }

                    // 후위표기 변환 완료 시, 실제 계산 수행
                    if (WillBreak == 0)
                    {
                        // 스택에 남은 연산자들 모두 pop
                        while (!isStackEmpty(MathStack))
                        {
                            postfix[y] = PopOp(MathStack);
                            y++;
                        }

                        postfix[y] = '\0'; // 문자열 끝

                        // 후위식 계산 단계
                        i = 0;
                        CalcStack->top = NULL;
                        while (postfix[i] != '\0')
                        {
                            if (isdigit((unsigned char)postfix[i]))
                            {
                                CalcStack = PushPostfix(postfix[i] - '0', CalcStack);
                            }
                            else if (postfix[i] == '+' || postfix[i] == '-' || postfix[i] == '*' || postfix[i] == '/')
                            {
                                val1 = PopPostfix(CalcStack);
                                val2 = PopPostfix(CalcStack);

                                // 실제 산술 계산
                                switch (postfix[i])
                                {
                                case '+': resultVal = val2 + val1; break;
                                case '-': resultVal = val2 - val1; break;
                                case '/': resultVal = val2 / val1; break;
                                case '*': resultVal = val2 * val1; break;
                                }

                                // 결과를 다시 스택에 push
                                CalcStack = PushPostfix(resultVal, CalcStack);
                            }
                            i++;
                        }

                        // 계산 결과를 저장
                        LastExpReturn = CalcStack->top->val; 
                    }
                    WillBreak = 0; // 함수 호출 처리 초기화
                }
            }
        }
    }

    // 파일 닫고 메모리 해제
    fclose(filePtr);
    STACK = FreeAll(STACK);

    // 종료 대기
    printf("\nPress a key to exit...");
    getch(); 
    return 0;
}
