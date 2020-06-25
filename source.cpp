#include <iostream>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <fstream>
#include <thread>
#include <tbb/parallel_for.h>
#include <tbb/concurrent_vector.h>
using namespace std;
namespace fs = filesystem;

pair<fs::path, fs::path> parseOpt (int argc, char const *argv[]) {
    if (argc != 3)
        throw std::logic_error("usage: fs_comparator dir1 dir2");
    fs::path path1(argv[1]);
    fs::path path2(argv[2]);
    
    if (!fs::exists(path1))
        throw std::logic_error("path '"s + argv[1] + "' does not exist"s);

    if (!fs::exists(path2))
        throw std::logic_error("path '"s + argv[2] + "' does not exist"s);

    if (!fs::is_directory(path1))
        throw std::logic_error("path '"s + argv[1] + "' is not a directory"s);

    if (!fs::is_directory(path2))
        throw std::logic_error("path '"s + argv[2] + "' is not a directory"s);

    return {fs::canonical(path1), fs::canonical(path2)};
}

class DirTraverser {
    fs::path Path1;
    fs::path Path2;
    string Path1Str;
    string Path2Str;
public:
    vector<fs::path> Only_at_first;
    vector<fs::path> Only_at_second;
    vector<string> Common_files;

    DirTraverser(fs::path const & path1, fs::path const & path2)
        : Path1(path1)
        , Path2(path2)
        , Path1Str(path1.string())
        , Path2Str(path2.string())
    {}

    void operator()() {
        if (fs::is_directory(Path1) && fs::is_directory(Path2)) {
            vector<string> paths1;
            vector<string> paths2;
            thread th(&::DirTraverser::traverse1, this, Path1, ref(Path1Str), ref(paths1));
            traverse1(Path2, ref(Path2Str), paths2);
            th.join();
            cout << '\n';
            split(paths1, paths2);
            cout << '\n';
            return;
        }
    }

private:

    bool is_parent(string_view parent, string_view child) const {
        return child.size() > parent.size() && child.substr(0, parent.size()) == parent;
    }

    void skip_(fs::path const & path, vector<string>::iterator & it, vector<string>::iterator const & iend) const {
        if (fs::is_directory(path)) {
            string_view parent = *it;
            for (++it; it != iend && is_parent(parent, *it); ++it);
        } else {
            ++it;
        }
    }

    void split(vector<string> paths1, vector<string> paths2) {
        size_t gc = 0;
        auto skip = [&](fs::path const & path, vector<string>::iterator & it, vector<string>::iterator const & iend, vector<fs::path> & out) {
            auto path_ = path / *it;
            skip_(path_, it, iend);
            out.push_back(move(path_));
        };
        auto it1 = paths1.begin();
        auto it2 = paths2.begin();
        while (it1 != paths1.end() && it2 != paths2.end()) {
            auto cmp_res = it1->compare(*it2);
            if (cmp_res != 0) {
                if (cmp_res < 0) {
                    skip(Path1, it1, paths1.end(), Only_at_first);
                } else {
                    skip(Path2, it2, paths2.end(), Only_at_second);
                }
                continue;
            }
            auto path1 = Path1 / *it1;
            auto path2 = Path2 / *it2;
            auto is_path1_dir = fs::is_directory(path1);
            if (is_path1_dir != fs::is_directory(path2)) {
                Only_at_first.push_back(move(path1));
                Only_at_second.push_back(move(path2));
            } else  if (!is_path1_dir) {
                Common_files.push_back(*it1);
                if (++gc % 1000 == 0) {
                    cout << '.';
                    cout.flush();
                }
            }
            ++it1;
            ++it2;
        }
        while (it1 != paths1.end())
            skip(Path1, it1, paths1.end(), Only_at_first);
        while (it2 != paths2.end())
            skip(Path2, it2, paths2.end(), Only_at_second);
    }

    string relative(fs::path const & path, string const & base) const {
        if (base == "/")
            return path.string().substr(1);
        return path.string().substr(base.size() + 1);
    }

    void traverse1(fs::path const & path, reference_wrapper<string const> PathStr, reference_wrapper<vector<string>> res) const noexcept {
        size_t gc = 0;
        for (auto it = fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied);
            it != fs::recursive_directory_iterator(); ++it)
        {
            try{
                auto const & x = *it;
                if ((fs::is_directory(x) || fs::is_regular_file(x)) && !fs::is_symlink(x)) {
                    res.get().push_back(relative(x.path(), PathStr));
                    ++gc;
                    if (gc % 1000 == 0) {
                        cout << '!';
                        cout.flush();
                    }
                }
            } catch (...){}
        }
        std::sort(res.get().begin(), res.get().end());
    }
};

bool compare(string const & s, fs::path const & path1, fs::path const & path2) {
    try {
        if (!fs::is_regular_file(path1 / s) || !fs::is_regular_file(path2 / s)) {
            throw runtime_error("wtf");
        }
        ifstream file1(path1 / s, ios::binary);
        ifstream file2(path2 / s, ios::binary);
        if (!file1.is_open() || !file2.is_open())
            return true;
        istreambuf_iterator<char> inp1(file1);
        istreambuf_iterator<char> inp2(file2);
        istreambuf_iterator<char> iend;
        for (; inp1 != iend  && inp2 != iend; ++inp1, ++inp2) {
            if (*inp1 != *inp2)
                return false;
        }
        return inp1 == iend && inp2 == iend;
    } catch (...) {
        return true;
    }
}

tbb::concurrent_vector<string> compare_all(std::vector<string> const & paths, fs::path const & path1, fs::path const & path2) {
    tbb::concurrent_vector<string> res;
    tbb::atomic<size_t> gc = 0;
    auto proc = [&](tbb::blocked_range<size_t> const & range) {
        for (size_t i = range.begin(); i != range.end(); ++i) {
            if (!compare(paths[i], path1, path2))
                res.push_back(paths[i]);
            if (++gc % 1000 == 0) {
                cout << '~';
                cout.flush();
            }
        }
    };
    tbb::parallel_for(tbb::blocked_range<size_t>(0,paths.size()), proc);
    std::sort(res.begin(), res.end());
    cout << '\n';
    return res;
}

class Logger {
    ofstream logg;
public:
    Logger(string filename) 
        : logg(filename)
    {
        if (!logg.is_open())
            throw runtime_error("cannot create file for logger :(");
    }

    template <class T>
    Logger & operator << (T && val) {
        cout << val;
        logg << val;
        return *this;
    }
};

int main(int argc, char const *argv[]) {
    try {
        auto args = parseOpt(argc, argv);
        DirTraverser traverser(args.first, args.second);
        traverser();
        Logger logg("fs_comparator_report");
        logg << "Has only at first path: " << traverser.Only_at_first.size() << '\n';
        for (auto const & x : traverser.Only_at_first)
            logg << x << '\n';
        logg << "Has only at second path: " << traverser.Only_at_second.size() << '\n';
        for (auto const & x : traverser.Only_at_second)
            logg << x << '\n';
        logg << "Common paths: " << traverser.Common_files.size() << '\n';
        auto total_common_diff = compare_all(traverser.Common_files, args.first, args.second);
        logg << "Total common diff: " << total_common_diff.size() << '\n';
        for (auto const x : total_common_diff)
            logg << x << '\n';

    } catch (exception const & e) {
        cerr << e.what() << '\n';
        return -1;
    }
    return 0;
}
