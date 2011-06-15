import qm.test.classes.command
import re

def _validate_output(self, stdout, stderr, result):
    causes = []
    stdout_re = re.compile(self.stdout + r'\n?\Z', re.MULTILINE | re.DOTALL)
    stderr_re = re.compile(self.stderr + r'\n?\Z', re.MULTILINE | re.DOTALL)
    if not stdout_re.match(stdout):
        causes.append('standard output')
        result["ExecTest.expected_stdout"] = result.Quote(self.stdout)
    if not stderr_re.match(stderr):
        causes.append('standard error')
        result["ExecTest.expected_stderr"] = result.Quote(self.stderr)
    return causes

class ExecTest(qm.test.classes.command.ExecTest):
    """Check a program's output and exit code, with a regex for the output."""

    ValidateOutput = _validate_output

class ShellCommandTest(qm.test.classes.command.ShellCommandTest):
    """Check a shell command's output and exit code, with a regex for the output."""

    ValidateOutput = _validate_output
