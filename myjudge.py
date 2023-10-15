from argparse import ArgumentParser
import subprocess
import socket
import shutil
import fcntl
import time
import sys
import os

source = ["Makefile", "server.c", "client.c", "hw1.h"]
executable = ["server", "client"]
testpath = "mytestcases"
timeout = 0.2
parser = ArgumentParser()
parser.add_argument("-t", "--task", choices=["1_1", "1_2", "1_3", "1_4", "1_5"], nargs="+")
parser.add_argument("-g", dest='generate', action="store_true")
parser.add_argument("-d", dest='debug', action="store_true")
parser.add_argument("-v", dest='verbose', action="store_true")
args = parser.parse_args()

class Checker(): # {{{
    def __init__(self):
        self.score = 0
        self.punishment = 0
        self.fullscore = sum(scores)
        self.io = sys.stderr

    def file_miss(self, files):
        return len(set(files) - set(os.listdir(".")))

    def remove_file(self, files):
        for f in files:
            os.system(f"rm -rf {f}")

    def compile(self):
        ret = os.system("make 1>/dev/null 2>/dev/null")
        if ret != 0 or self.file_miss(executable):
            return False
        return True
        
    def makeclean(self):
        ret = os.system("make clean 1>/dev/null 2>/dev/null")
        if ret != 0 or self.file_miss(executable)!=len(executable):
            self.remove_file(executable)
            return False
        return True

    def run(self):
        print("Checking file format ...", file=self.io)
        if self.file_miss(source):
            print("File not found", file=self.io)
            exit()

        if self.file_miss(executable) != len(executable):
            self.punishment += 0.25
            red("Find executable files", self.io)
            self.remove_file(executable)

        print("Compiling source code ...", file=self.io)
        if not self.compile():
            print("Compiled Error", file=self.io)
            exit()

        print("Testing make clean command ...", file=self.io)
        if not self.makeclean():
            self.punishment += 0.25
            print("Make clean failed", file=self.io)

        self.compile()
        for t, s in zip(testcases, scores):
            self.remove_file(["./BulletinBoard"])
            if t(self.io):
                self.score += s
        
        self.remove_file(executable)

        self.score = max(0, self.score-self.punishment)
        print(f"Final score: {self.score} / {self.fullscore}", file=self.io)
# }}}
class Server(): #{{{
    def __init__(self, port):
        self.log = ""
        if args.debug:
            self.p = subprocess.Popen(["./server", str(port)], stderr=subprocess.STDOUT, stdout=subprocess.PIPE)
        else:
            self.p = subprocess.Popen(["./server", str(port)], stderr=subprocess.DEVNULL, stdout=subprocess.PIPE)
        time.sleep(timeout)

    def exit(self):
        try:
            self.p.terminate()
            self.log = self.p.communicate(timeout=timeout)[0].decode()
            return self.log
        except Exception as e:
            self.p.kill()
            self.p.wait()
            raise Exception(f"Server exit {e}")
#}}}
class Client(): #{{{
    def __init__(self, port):
        self.log = ""
        self.p = subprocess.Popen(["./client", "127.0.0.1", str(port)], stdin=subprocess.PIPE, stderr=subprocess.DEVNULL, stdout=subprocess.PIPE)
        time.sleep(timeout)

    def zuoshi(self, instructions):
        ptr = 0
        while ptr < len(instructions):
            time.sleep(timeout)
            try:
                res = instructions[ptr] + '\n'
                ptr += 1
                self.p.stdin.write(f"{res}".encode())
                self.p.stdin.flush()
            except Exception as e:
                self.p.kill()
                self.p.wait()
                raise e
    def inputfile(self, filename):
        inputs = open(filename, "r")
        while True:
            try:
                res = inputs.readline()
                if res == "pull\n":
                    self.pull()
                if res == "post\n":
                    fr = inputs.readline()
                    co = inputs.readline()
                    self.post(fr, co)
                if res == "exit\n":
                    return self.exit()
                if res[0] == '#':
                    mytimeout = int(res[1:])
                    time.sleep(mytimeout)
            except Exception as e:
                self.p.kill()
                self.p.wait()
                raise e
                
    def pull(self):
        try:
            self.p.stdin.write(b"pull\n")
            self.p.stdin.flush()
            time.sleep(timeout)
        except Exception as e:
            raise Exception(f"Client pull {e}")
    
    def post(self, f, c):
        try:
            self.p.stdin.write(f"post\n{f}{c}".encode())
            self.p.stdin.flush()
            time.sleep(timeout)
        except Exception as e:
            raise Exception(f"Client post {e}")
    
    def exit(self):
        try:
            self.log = self.p.communicate(input="exit\n".encode(), timeout=timeout)[0].decode()
            return self.log
        except Exception as e:
            self.p.kill()
            self.p.wait()
            raise Exception(f"Client exit {e}")
# }}}
def runner(testname, io, gen=args.generate): #{{{
    try:
        board = "./BulletinBoard"
        file = open(f"{testpath}/{testname}/instructions", 'r')
        shutil.copy2(f"{testpath}/{testname}/init", board)
        bold(f"===== {testname} =====", io)
       
        server = []
        ports = []
        client = []
        serverlog = []
        clientlog = []
        __cnt = 0
        while True:
            time.sleep(timeout)
            x = file.readline()
            __cnt += 1
            if args.verbose:
                red(str(__cnt) + ' ' + x[:-1], io)
            if len(x) == 0:
                break
            x = x[:-1].split(' ');
            if x[0] == 'init_server':
                cnt = int(x[1])
                ports = [find_empty_port() for _ in range(cnt)]
                server = [Server(ports[i]) for i in range(cnt)]
                for i in range(cnt):
                    serverlog.append("")
            if x[0] == 'init_client':
                num = int(x[1])
                cnt = int(x[2])
                for i in range(cnt):
                    client.append(Client(ports[num]))
                    clientlog.append("")
            if x[0] == 'send':
                cli = int(x[1])
                client[cli].zuoshi(x[2:])
            if x[0] == 'exit_client':
                cli = int(x[1])
                clientlog[cli] = client[cli].exit()
            if x[0] == 'exit_server':
                ser = int(x[1])
                serverlog[ser] = server[ser].exit()

        if gen:
            shutil.copy2(board, f"{testpath}/{testname}/record")
            for i in range(len(serverlog)):
                serverfile = open(f"{testpath}/{testname}/server{i+1}", "w")
                print(serverlog[i], end='', file=serverfile)
            for i in range(len(clientlog)):
                clientfile = open(f"{testpath}/{testname}/client{i+1}", 'w')
                print(clientlog[i], end='', file=clientfile)
        else:   
            ok = compare(board, f"{testpath}/{testname}/record")
            print(f"record: {ok}")
            for i in range(len(serverlog)):
                cur = strcompare(serverlog[i], f"{testpath}/{testname}/server{i+1}")
                ok = ok and cur
                print(f"server {i}: {cur}")
            for i in range(len(clientlog)):
                cur = strcompare(clientlog[i], f"{testpath}/{testname}/client{i+1}")
                ok = ok and cur
                print(f"client {i}: {cur}")
            if not ok:
                print("Ok")
                assert False
            if not compare(board, f"{testpath}/{testname}/record"):
                print("Compare")
                assert False
            for i in range(len(serverlog)):
                assert strcompare(serverlog[i], f"{testpath}/{testname}/server{i+1}")
            for i in range(len(clientlog)):
                assert strcompare(clientlog[i], f"{testpath}/{testname}/client{i+1}")

        green(f"{testname}: passed", io)
        return True
    except Exception as e:
        print(repr(e))
        red(f"{testname}: failed", io)
        return False
#}}}
def testcase1_1(io):
    return runner("testcase1-1", io)
def testcase1_2(io):
    return runner("testcase1-2", io)
def testcase1_3(io):
    return runner("testcase1-3", io)
def testcase1_4(io):
    return runner("testcase1-4", io)
def testcase1_5(io):
    return runner("testcase1-5", io)

def read_record(filename="./BulletinBoard"): # {{{
    rec = []
    factor = 1
    with open(filename, "rb") as f:
        content = f.read()
        for i in range(0, len(content), 25 * factor):
            f = ""
            for ch in content[i:i+5 * factor]:
                if ch == 0:
                    break
                f += chr(ch)
            c = ""
            for ch in content[i+5 * factor:i+25 * factor]:
                if ch == 0:
                    break
                c += chr(ch)
            rec.append({"FROM":f, "CONTENT":c})
        for _ in range(len(rec), 10):
            rec.append({"FROM":"", "CONTENT":""})
    return rec
#}}}
# just some utility functions {{{
def compare(A, B):
    a = read_record(A)
    b = read_record(B)
    for i, j in zip(a, b):
        if i["FROM"] != j["FROM"]:
            return False
        if i["CONTENT"] != j["CONTENT"]:
            return False
    return True

def strcompare(contentA, B):
    with open(B, "r") as b:
        contentB = b.read()
    return contentA == contentB

def bold(str_, io):
    print("\33[1m" + str_ + "\33[0m", file=io)

def red(str_, io):
    print("\33[31m" + str_ + "\33[0m", file=io)

def green(str_, io):
    print("\33[32m" + str_ + "\33[0m", file=io)

def find_empty_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(('localhost', 0))
    _, port = s.getsockname()
    s.close()
    return port
# }}} 

if __name__ == "__main__":
    testcases = [testcase1_1, testcase1_2, testcase1_3, testcase1_4, testcase1_5]
    scores = [0.2, 0.2, 0.2, 0.2, 0.2]
    index = {"1_1":0, "1_2": 1, "1_3": 2, "1_4" : 3, "1_5" : 4}
    if args.task is not None:
        task = []
        for t in args.task:
            task.append(index[t])
        task.sort()
        testcases = [testcases[i] for i in task]
        scores = [scores[i] for i in task]
    Checker().run()
