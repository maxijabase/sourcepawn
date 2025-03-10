# vim: set ts=4 sw=4 tw=99 et:
import argparse
import os
import progressbar
import queue
import re
import shutil
import subprocess
import sys
import tempfile
import threading

# Tool for interacting with a .sp corpus to find behavorial differences between
# compiler versions.
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('spcomp', type = str, help = 'Path to spcomp')
    parser.add_argument('corpus', type = str, help = 'Path to plugin corpus')
    parser.add_argument('-i', '--include', type = str, action = 'append', default = [],
                        help = 'Extra include paths')
    parser.add_argument('--remove-bad', action = 'store_true', default = False,
                        help = 'Remove bad .sp files on failure')
    parser.add_argument('--commit', action = 'store_true', default = False,
                        help = 'Run destructive commands instead of printing them')
    parser.add_argument('--diagnose', action = 'store_true', default = False,
                        help = 'Interactive diagnose script')
    parser.add_argument("--remove-good", action = 'store_true', default = False,
                        help = 'Remove good .sp failes on success')
    parser.add_argument("-j", type = int, default = 1,
                        help = "Number of compile jobs; does not work with --diagnose")
    parser.add_argument("--verifier", type = str, default = None,
                        help = "Optional verification tool for .smx files")
    parser.add_argument("--collect-smx", type = str, default = None,
                        help = "Copy .smx files to the given folder")
    parser.add_argument("--log", type = str, default = None,
                        help = "Write output log to a file")
    parser.add_argument("--retry-bad", default = False, action = 'store_true',
                        help = "Retry previously known bad scripts")

    args = parser.parse_args()

    if args.j > 1 and args.diagnose:
        print("Cannot use both -j and --diagnose.")
        return 1

    files = []
    get_all_files(args.corpus, ['.sp', '.smx'], files)

    with tempfile.TemporaryDirectory() as temp_dir:
        runner = Runner(args, files, temp_dir)
        runner.run()

class Runner(object):
    def __init__(self, args, files, temp_dir):
        self.args_ = args
        self.files_ = files
        self.temp_dir_ = temp_dir
        self.threads_ = []
        self.work_ = queue.LifoQueue()
        self.completed_ = queue.Queue()
        self.progress_ = 0
        self.skip_set_ = set()
        self.missing_includes_ = {}
        self.log_ = sys.stderr

        self.includes_ = [os.path.join(self.args_.corpus, 'include')]
        self.includes_.extend(args.include)

        if self.args_.log:
            self.log_ = open(self.args_.log, 'wt')

        more_includes = os.path.join(self.args_.corpus, 'corpus_include.list')
        if os.path.exists(more_includes):
            with open(more_includes, 'rt') as fp:
                for line in fp.readlines():
                    include = os.path.join(self.args_.corpus, line.strip())
                    self.includes_.append(include)

        self.skip_file_path_ = os.path.join(self.args_.corpus, 'corpus_skip.list')
        if os.path.exists(self.skip_file_path_) and not self.args_.retry_bad:
            with open(self.skip_file_path_, 'rt') as fp:
                for line in fp.readlines():
                    self.skip_set_.add(line.strip())

        self.files_ = [os.path.relpath(file, self.args_.corpus) for file in self.files_]
        self.files_ = [file for file in self.files_ if file not in self.skip_set_]

    def run(self):
        with progressbar.ProgressBar(max_value = len(self.files_), redirect_stdout = True) as bar:
            if self.args_.j <= 1:
                self.run_st(bar)
            else:
                self.run_mt(bar)

        missing = sorted(self.missing_includes_.items(), key=lambda item: item[1])
        for include, encounters in missing:
            print("Missing include {} used {} times.".format(include, encounters))

        # Re-sort the skip list.
        if os.path.exists(self.skip_file_path_):
            skip_set = set()
            with open(self.skip_file_path_, 'rt') as fp:
                for line in fp.readlines():
                    skip_set.add(line.strip())

            with open(self.skip_file_path_, 'wt') as fp:
                for path in sorted(skip_set):
                    fp.write(path + "\n")

    def run_st(self, bar):
        for i in range(len(self.files_)):
            result_tuple = self.compile(self.files_[i])
            self.handle_result(result_tuple)

            bar.update(i)

    def run_mt(self, bar):
        for file in self.files_:
            self.work_.put(file)

        for i in range(self.args_.j):
            thread = threading.Thread(None, self.consumer)
            self.threads_.append(thread)
            thread.start()

        while self.progress_ < len(self.files_):
            result_tuple = self.completed_.get()
            self.handle_result(result_tuple)
            bar.update(self.progress_)
            self.progress_ += 1

        for thread in self.threads_:
            thread.join()

    def consumer(self):
        while True:
            try:
                item = self.work_.get_nowait()
            except queue.Empty:
                item = None
            if item is None:
                if self.work_.empty():
                    break
                continue
            result_tuple = self.compile(item)
            self.completed_.put(result_tuple)

    def compile(self, path):
        if path.endswith('.sp'):
            argv = [
                self.args_.spcomp,
                os.path.join(self.args_.corpus, path),
            ]
            for include_path in self.includes_:
                argv += ['-i', include_path]

            output_file = os.path.join(self.temp_dir_, os.path.basename(path))
            if output_file.endswith('.sp'):
                output_file = output_file[:-3]
                output_file += '.smx'

            argv += ['-o', output_file]

            ok = False
            output = None
            try:
                subprocess.check_output(argv, stderr = subprocess.STDOUT, timeout = 10)
                ok = True
            except KeyboardInterrupt:
                raise
            except subprocess.CalledProcessError as e:
                output = e.output
                output = output.decode('utf-8', errors = 'ignore')
            except subprocess.TimeoutExpired as e:
                output = "timed out"
            except:
                pass
        else:
            ok = True
            output = ''
            output_file = os.path.join(self.args_.corpus, path)
            argv = []

        if ok and self.args_.verifier:
            argv = [self.args_.verifier, output_file]
            ok = False
            try:
                subprocess.check_output(argv, stderr = subprocess.STDOUT, timeout = 10)
                ok = True
            except KeyboardInterrupt:
                raise
            except subprocess.CalledProcessError as e:
                output = e.output
                output = output.decode('utf-8', errors = 'ignore')
            except subprocess.TimeoutExpired as e:
                output = "timed out"
            except:
                pass

        if ok and path.endswith('.sp') and self.args_.collect_smx:
            shutil.move(output_file,
                        os.path.join(self.args_.collect_smx, os.path.basename(output_file)))

        return (ok, path, output, argv)

    def handle_result(self, result_tuple):
        ok, path, output, argv = result_tuple

        if output is None:
            output = ""

        remove = False
        if not ok:
            if output == "timed out":
                self.log_.write("timed out: " + path + "\n")
            else:
                self.log_.write("failed: " + path + "\n")
            self.log_.write("    " + ' '.join(argv) + "\n")

            if self.args_.diagnose:
                remove = diagnose_error(os.path.join(self.args_.corpus, path), output)
            elif self.args_.remove_bad:
                if self.args_.commit:
                    remove = True

            m = re.search("cannot read from file: \"(.+)\"", output)
            if m is not None:
                include = m.group(1)
                self.missing_includes_[include] = self.missing_includes_.get(include, 0) + 1
        else:
            if self.args_.remove_good:
                self.log_.write("rm \"{}\"".format(path) + "\n")
                if self.args_.commit:
                    remove = True

        if remove:
            with open(self.skip_file_path_, 'at') as fp:
                fp.write(path + "\n")

def diagnose_error(path, output):
    print("Error compiling {}:".format(path))
    print("")

    lines = [line.strip() for line in output.split('\n')]
    for line in lines:
        m = re.search(r"\((\d+)\) : error", line)
        if m is None:
            continue

        message = line[m.start():]
        print("  " + message)
        print("")

        diag = extract_line(path, int(m.group(1)))
        if diag is None:
            print("   >> Unknown line <<")
        else:
            print("  " + diag.strip())
        print("")

    while True:
        sys.stdout.write("(D)elete or (S)kip? ")
        progressbar.streams.flush()
        line = sys.stdin.readline()
        line = line.strip()
        if line == 'D' or line == 'd':
            return True
        elif line == 'S' or line == 's':
            return False

def extract_line(path, number):
    with open(path, 'rb') as fp:
        data = fp.read()
    text = data.decode('utf8', 'ignore')
    lines = text.split('\n')
    if number - 1 >= len(lines):
        return None
    return lines[number - 1]

def get_all_files(path, exts, out):
    for file in os.listdir(path):
        child = os.path.join(path, file)
        if file == 'corpus.list':
            import_corpus_list(path, child, out)
            return
        if os.path.isdir(child):
            get_all_files(child, exts, out)
            continue
        _, ext = os.path.splitext(child)
        if ext in exts:
            out.append(child)

def import_corpus_list(root, list_file, out):
    with open(list_file, 'rt') as fp:
        for line in fp.readlines():
            out.append(os.path.join(root, line.strip()))

if __name__ == '__main__':
    main()
